#include "P4GamePreviewWorld.h"
#include "P4GameNames.h"
#include "P4AuthoringRows.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "P4PlacementExport.h"
#include "AssetRegistryModule.h"
#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemConfig.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NavigationSystem.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaxPackages = 4096;

bool GeneratedPreviewPackage(const FString& Package)
{
    const FString Prefix = TEXT(P4_PREVIEW_PACKAGE_ROOT);
    if (!Package.StartsWith(Prefix, ESearchCase::CaseSensitive))
        return false;
    FString Identity = Package.Mid(Prefix.Len());
    int32 Slash = INDEX_NONE;
    if (!Identity.FindChar(TCHAR('/'), Slash))
        return false;
    Identity.LeftInline(Slash);
    if (Identity.Len() != 49 || Identity[32] != TCHAR('_'))
        return false;
    for (int32 At = 0; At < Identity.Len(); ++At)
        if (At != 32 && !FChar::IsHexDigit(Identity[At]))
            return false;
    return true;
}

bool GameSource(UObject* Object)
{
    return P4IsGamePreviewGameSource(Object);
}

UObject* PackageAsset(const FString& Package)
{
    UPackage* Loaded = FindPackage(nullptr, *Package);
    if (!Loaded)
        Loaded = LoadPackage(nullptr, *Package, LOAD_NoWarn | LOAD_Quiet);
    if (!Loaded)
        return nullptr;
    const FString Name = FPackageName::GetLongPackageAssetName(Package);
    if (UObject* Asset = FindObject<UObject>(Loaded, *Name))
        return Asset;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Loaded, Objects, false);
    for (UObject* Object : Objects)
        if (Object && Object->IsAsset() && !Object->HasAnyFlags(RF_Transient))
            return Object;
    return nullptr;
}

bool EngineFile(UObject* Source)
{
    if (!IsValid(Source) || Source->GetOuter() != Source->GetOutermost() ||
        !Source->GetOutermost()->GetName().StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive))
        return false;
    FString File;
    if (!FPackageName::DoesPackageExist(Source->GetOutermost()->GetName(), nullptr, &File))
        return false;
    File = FPaths::ConvertRelativePathToFull(File);
    const FString EngineContent = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir());
    return FPaths::IsUnderDirectory(File, EngineContent) && IFileManager::Get().FileSize(*File) > 0;
}

struct FEngineProof
{
    const FP4GamePreviewEngineCopies* Owner;
    TWeakObjectPtr<UObject> Source;
    FString CopyPath, SourcePath;
};
TMap<const UObject*, FEngineProof> EngineProofs;
struct FDerivedProof
{
    const FP4GamePreviewDerivedCopies* Owner;
    TWeakObjectPtr<UObject> Source;
    TWeakObjectPtr<UWorld> World;
    FString CopyPath, SourcePath;
};
TMap<const UObject*, FDerivedProof> DerivedProofs;
struct FMaterialInstanceProof
{
    const FP4GamePreviewMaterialInstances* Owner;
    TWeakObjectPtr<UObject> Source;
    TWeakObjectPtr<UWorld> World, CopyWorld;
    TWeakObjectPtr<UMaterialInterface> SourceParent;
    FString CopyPath, SourcePath;
};
TMap<const UObject*, FMaterialInstanceProof> MaterialInstanceProofs;

void WorldBuildData(UWorld* World, TSet<UObject*>& Out)
{
    if (!IsValid(World) || GameSource(World))
        return;
    TArray<ULevel*> Levels = World->GetLevels();
    Levels.AddUnique(World->PersistentLevel);
    for (ULevel* Level : Levels)
    {
        if (!IsValid(Level) || Level->HasAnyFlags(RF_Transient))
            continue;
        UWorld* LevelWorld = Level->GetTypedOuter<UWorld>();
        UMapBuildDataRegistry* Registry = Level->MapBuildData;
        if (!LevelWorld || GameSource(LevelWorld) || !IsValid(Registry) ||
            Registry->GetClass() != UMapBuildDataRegistry::StaticClass() || GameSource(Registry))
            continue;
        Out.Add(Registry);
        // AddReferencedObjects includes native light/shadow maps and
        // reflection captures that are absent from reflected fields.
        // Only their real references within this registry's package count.
        TArray<UObject*> References;
        FReferenceFinder Finder(References, Registry->GetOutermost(), false, true, false, true);
        Finder.FindReferences(Registry);
        for (UObject* Object : References)
            if (IsValid(Object) && Object->IsA<UTexture>() && !Object->HasAnyFlags(RF_Transient) && !GameSource(Object))
                Out.Add(Object);
    }
}
struct FSceneProof
{
    const FP4GamePreviewSceneAssets* Owner;
    FString ObjectPath, ClassPath;
};
TMap<const UObject*, FSceneProof> SceneProofs;
} //namespace

bool P4IsGamePreviewGameSource(UObject* Object)
{
    // The line the release export draws with P4IsCreatorAsset: only the game's own asset at its own
    // path is the game's. A copy of a game asset in a mod is the creator's and goes into Play.
    if (!IsInGameThread() || !IsValid(Object))
        return false;
    FString Origin;
    return P4QueryOriginalAsset(Object).IsOriginal() || P4IsVerifiedStandIn(Object) ||
        (P4HasIntrinsicGameSource(Object, &Origin) && Origin == Object->GetOutermost()->GetName());
}

FString P4GamePreviewGameplayActorIssue(UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
        return FString();
    TArray<FString> Names;
    for (const AActor* Actor : World->PersistentLevel->Actors)
        if (IsValid(Actor) && P4IsGameplayActor(Actor))
            Names.Add(Actor->GetActorLabel());
    if (!Names.Num())
        return FString();
    Names.Sort();
    const int32 Shown = FMath::Min(Names.Num(), 5);
    FString List;
    for (int32 At = 0; At < Shown; ++At)
        List += (At == 0                                            ? TEXT("")
                        : (At == Shown - 1 && Names.Num() == Shown) ? TEXT(" and ")
                                                                    : TEXT(", ")) +
            Names[At];
    if (Names.Num() > Shown)
        List += FString::Printf(TEXT(" and %d more"), Names.Num() - Shown);
    // Export takes gameplay objects only into a game map's placement list, so a level of the
    // creator's own cannot carry them into the game, and neither can Play.
    return FString::Printf(
        TEXT(
            "Play can't run gameplay objects that are in a level of your own yet. Remove %s from %s, or place them in a game map and press Play there."),
        *List, *FPackageName::GetShortName(World->GetOutermost()->GetName()));
}

struct FP4GamePreviewEngineCopies::FImpl
{
    TArray<TStrongObjectPtr<UObject>> Copies, Sources;
};

FP4GamePreviewEngineCopies::FP4GamePreviewEngineCopies() : Impl(MakeUnique<FImpl>())
{}

FP4GamePreviewEngineCopies::~FP4GamePreviewEngineCopies()
{
    for (const auto& Copy : Impl->Copies)
        if (const FEngineProof* Proof = EngineProofs.Find(Copy.Get()))
            if (Proof->Owner == this)
                EngineProofs.Remove(Copy.Get());
}

bool FP4GamePreviewEngineCopies::Add(UObject* Copy, UObject* Source, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Copy) || !IsValid(Source) || Copy == Source ||
        Copy->GetClass() != Source->GetClass() || !EngineFile(Source) ||
        !GeneratedPreviewPackage(Copy->GetOutermost()->GetName()) || GameSource(Copy) || GameSource(Source))
    {
        Error = TEXT(
            "Private preview engine content must be a native copy of an installed engine asset in the generated preview namespace.");
        return false;
    }
    if (const FEngineProof* Existing = EngineProofs.Find(Copy))
    {
        if (Existing->Owner == this && Existing->Source.Get() == Source)
            return true;
        Error = TEXT("Another preview scope owns this engine content copy.");
        return false;
    }
    Impl->Copies.Emplace(Copy);
    Impl->Sources.Emplace(Source);
    EngineProofs.Add(Copy, {this, Source, Copy->GetPathName(), Source->GetPathName()});
    return true;
}

bool P4IsTrustedGamePreviewEngineCopy(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    const FEngineProof* Proof = EngineProofs.Find(Asset);
    UObject* Source = Proof ? Proof->Source.Get() : nullptr;
    return Source && Source->GetClass() == Asset->GetClass() && Asset->GetPathName() == Proof->CopyPath &&
        Source->GetPathName() == Proof->SourcePath && GeneratedPreviewPackage(Asset->GetOutermost()->GetName());
}

struct FP4GamePreviewDerivedCopies::FImpl
{
    TArray<TStrongObjectPtr<UObject>> Copies, Sources;
    TArray<TStrongObjectPtr<UWorld>> Worlds;
};

FP4GamePreviewDerivedCopies::FP4GamePreviewDerivedCopies() : Impl(MakeUnique<FImpl>())
{}

FP4GamePreviewDerivedCopies::~FP4GamePreviewDerivedCopies()
{
    for (const auto& Copy : Impl->Copies)
        if (const FDerivedProof* Proof = DerivedProofs.Find(Copy.Get()))
            if (Proof->Owner == this)
                DerivedProofs.Remove(Copy.Get());
}

bool FP4GamePreviewDerivedCopies::Add(UObject* Copy, UObject* Source, UWorld* World, FString& Error)
{
    Error.Reset();
    TSet<UObject*> Sources;
    if (IsInGameThread())
        WorldBuildData(World, Sources);
    if (!IsInGameThread() || !IsValid(Copy) || !IsValid(Source) || Copy == Source ||
        Copy->GetClass() != Source->GetClass() || !Sources.Contains(Source) ||
        !GeneratedPreviewPackage(Copy->GetOutermost()->GetName()) || GameSource(Copy))
    {
        Error = TEXT(
            "Private preview build data must copy the authored level's actual lighting registry or one of its own referenced textures.");
        return false;
    }
    if (const FDerivedProof* Existing = DerivedProofs.Find(Copy))
    {
        if (Existing->Owner == this && Existing->Source.Get() == Source && Existing->World.Get() == World)
            return true;
        Error = TEXT("Another preview scope owns this built-data copy.");
        return false;
    }
    Impl->Copies.Emplace(Copy);
    Impl->Sources.Emplace(Source);
    Impl->Worlds.Emplace(World);
    DerivedProofs.Add(Copy, {this, Source, World, Copy->GetPathName(), Source->GetPathName()});
    return true;
}

bool P4IsGamePreviewDerivedCopy(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    const FDerivedProof* Proof = DerivedProofs.Find(Asset);
    UObject* Source = Proof ? Proof->Source.Get() : nullptr;
    UWorld* World = Proof ? Proof->World.Get() : nullptr;
    if (!Source || !World || Source->GetClass() != Asset->GetClass() || Asset->GetPathName() != Proof->CopyPath ||
        Source->GetPathName() != Proof->SourcePath || !GeneratedPreviewPackage(Asset->GetOutermost()->GetName()))
        return false;
    TSet<UObject*> Sources;
    WorldBuildData(World, Sources);
    return Sources.Contains(Source);
}

bool P4IsGamePreviewDerivedSource(const UObject* Asset, UWorld* AuthoredWorld)
{
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    TSet<UObject*> Sources;
    WorldBuildData(AuthoredWorld, Sources);
    return Sources.Contains(const_cast<UObject*>(Asset));
}

struct FP4GamePreviewMaterialInstances::FImpl
{
    TArray<TStrongObjectPtr<UObject>> Copies, Sources;
    TArray<TStrongObjectPtr<UWorld>> Worlds;
};

FP4GamePreviewMaterialInstances::FP4GamePreviewMaterialInstances() : Impl(MakeUnique<FImpl>())
{}

FP4GamePreviewMaterialInstances::~FP4GamePreviewMaterialInstances()
{
    for (const auto& Copy : Impl->Copies)
        if (const FMaterialInstanceProof* Proof = MaterialInstanceProofs.Find(Copy.Get()))
            if (Proof->Owner == this)
                MaterialInstanceProofs.Remove(Copy.Get());
}

bool FP4GamePreviewMaterialInstances::Add(UObject* Copy, UObject* Source, UWorld* World, FString& Error)
{
    Error.Reset();
    UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Copy);
    UMaterialInstanceDynamic* Original = Cast<UMaterialInstanceDynamic>(Source);
    UWorld* CopyWorld = IsValid(Copy) ? Copy->GetTypedOuter<UWorld>() : nullptr;
    if (!IsInGameThread() || !IsValid(Instance) || !IsValid(Original) || !IsValid(World) || Copy == Source ||
        CopyWorld == World || !IsValid(CopyWorld) || Instance->GetClass() != UMaterialInstanceDynamic::StaticClass() ||
        Original->GetClass() != UMaterialInstanceDynamic::StaticClass() || !Original->IsIn(World) ||
        GameSource(World) || GameSource(Original) || GameSource(Instance) ||
        !GeneratedPreviewPackage(CopyWorld->GetOutermost()->GetName()) ||
        !GeneratedPreviewPackage(Instance->GetOutermost()->GetName()) || !IsValid(Original->Parent) ||
        Original->Parent->GetClass() != UMaterial::StaticClass() || !EngineFile(Original->Parent) ||
        GameSource(Original->Parent) || !P4IsTrustedGamePreviewEngineCopy(Instance->Parent))
    {
        Error = TEXT(
            "A preview dynamic material must copy the authored world's actual instance and its proven installed-engine material parent.");
        return false;
    }
    const FEngineProof* ParentProof = EngineProofs.Find(Instance->Parent);
    if (!ParentProof || ParentProof->Source.Get() != Original->Parent)
    {
        Error = TEXT("The preview dynamic material's parent does not match its actual source parent.");
        return false;
    }
    if (const FMaterialInstanceProof* Existing = MaterialInstanceProofs.Find(Copy))
    {
        if (Existing->Owner == this && Existing->Source.Get() == Source && Existing->World.Get() == World)
            return true;
        Error = TEXT("Another preview scope owns this dynamic material copy.");
        return false;
    }
    Impl->Copies.Emplace(Copy);
    Impl->Sources.Emplace(Source);
    Impl->Worlds.Emplace(World);
    Impl->Worlds.Emplace(CopyWorld);
    MaterialInstanceProofs.Add(
        Copy, {this, Source, World, CopyWorld, Original->Parent, Copy->GetPathName(), Source->GetPathName()});
    return true;
}

bool P4IsGamePreviewMaterialInstance(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset) || Asset->GetClass() != UMaterialInstanceDynamic::StaticClass())
        return false;
    const FMaterialInstanceProof* Proof = MaterialInstanceProofs.Find(Asset);
    UMaterialInstanceDynamic* Source = Proof ? Cast<UMaterialInstanceDynamic>(Proof->Source.Get()) : nullptr;
    UWorld* World = Proof ? Proof->World.Get() : nullptr;
    UWorld* CopyWorld = Proof ? Proof->CopyWorld.Get() : nullptr;
    return Source && World && CopyWorld && Source->IsIn(World) && Asset->IsIn(CopyWorld) &&
        Source->GetClass() == Asset->GetClass() && Source->Parent == Proof->SourceParent.Get() &&
        Asset->GetPathName() == Proof->CopyPath && Source->GetPathName() == Proof->SourcePath &&
        GeneratedPreviewPackage(Asset->GetOutermost()->GetName());
}

bool P4PrepareGamePreviewWorld(UWorld* World, UWorld* Source, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || !IsValid(Source) || World == Source || !World->PersistentLevel ||
        !Source->PersistentLevel || World->GetOutermost() == Source->GetOutermost() ||
        !GeneratedPreviewPackage(World->GetOutermost()->GetName()) || GameSource(Source) || GameSource(World))
    {
        Error = TEXT("Game world settings can be prepared only on a separate private scratch preview clone.");
        return false;
    }
    UClass* GameSettings = LoadObject<UClass>(
        nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".MyWorldSettings"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    UClass* GameNavigation = LoadObject<UClass>(
        nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".CarrotNavigationSystem"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!GameSettings || !GameSettings->IsChildOf(AWorldSettings::StaticClass()) || !GameNavigation ||
        !GameNavigation->IsChildOf(UNavigationSystemBase::StaticClass()))
    {
        Error = TEXT(
            "The game's actual world settings and navigation classes must be available before preparing a scratch preview.");
        return false;
    }
    AWorldSettings* SourceSettings = Source->PersistentLevel->GetWorldSettings(false);
    AWorldSettings* Settings = World->PersistentLevel->GetWorldSettings(false);
    if (!IsValid(SourceSettings) || !IsValid(Settings) ||
        (SourceSettings->GetClass() != AWorldSettings::StaticClass() && SourceSettings->GetClass() != GameSettings) ||
        (Settings->GetClass() != AWorldSettings::StaticClass() && Settings->GetClass() != GameSettings))
    {
        Error = TEXT(
            "Scratch preview supports exact engine WorldSettings or the game's MyWorldSettings; other authored subclasses require an explicit adapter.");
        return false;
    }
    // A level saved with Engine.WorldSettings and loaded or duplicated in a project whose settings class is
    // the game's goes through the engine's UWorld::RepairWorldSettings (PostLoad). It renames the old actor,
    // spawns the game's one, copies only the values that differ from the old class's defaults into it, and
    // calls ULevel::SetWorldSettings, which calls Destroy on the old actor while it is still the current
    // one. DestroyActor never destroys the current settings actor, so the old one stays in the level. The
    // clone retires every such leftover here, so Play never ships a level with two settings actors.
    const auto OtherSettings = [World](AWorldSettings* Current)
    {
        TArray<AWorldSettings*> Others;
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (AWorldSettings* Other = Cast<AWorldSettings>(Actor))
                if (IsValid(Other) && Other != Current)
                    Others.AddUnique(Other);
        return Others;
    };
    const auto Retire = [World](AWorldSettings* Stale, AWorldSettings* Current)
    {
        TMap<UObject*, UObject*> Replacements;
        Replacements.Add(Stale, Current);
        FArchiveReplaceObjectRef<UObject> Remap(World, Replacements, false, true, true);
        if (World->PersistentLevel->GetWorldSettings(false) != Current || (IsValid(Stale) && !Stale->Destroy()))
            return false;
        TArray<AActor*>& Actors = World->PersistentLevel->Actors;
        for (int32 At = 0; At < Actors.Num(); ++At)
            if (Actors[At] == Stale || (At > 0 && Actors[At] == Current))
                Actors[At] = nullptr;
        return Actors.Num() > 0 && Actors[0] == Current;
    };
    if (Settings->GetClass() == GameSettings && SourceSettings->GetClass() == AWorldSettings::StaticClass())
    {
        // The source's settings are the engine's class and the clone's current ones are the game's, so the
        // clone's own duplication ran that repair: its leftover engine actor is the exact copy of the
        // authored settings, and the current one holds only the delta copy. The authored copy becomes
        // current again, and the full-value migration below starts from it.
        AWorldSettings* Authored = nullptr;
        int32 Plain = 0;
        for (AWorldSettings* Other : OtherSettings(Settings))
            if (Other->GetClass() == AWorldSettings::StaticClass())
            {
                Authored = Other;
                ++Plain;
            }
        // None left means this clone was prepared already: its current actor is the full migration.
        if (Plain > 1)
        {
            Error = FString::Printf(
                TEXT(
                    "The private level's settings were repaired by the engine but %d copies of the authored settings were found."),
                Plain);
            return false;
        }
        if (Plain == 1)
        {
            AWorldSettings* Repaired = Settings;
            // Its Destroy of the repaired actor is refused while that actor is still current; Retire finishes it.
            World->PersistentLevel->SetWorldSettings(Authored);
            if (!Retire(Repaired, Authored))
            {
                Error = TEXT("The engine-repaired private world settings actor could not be retired.");
                return false;
            }
            Settings = Authored;
        }
    }
    FObjectPropertyBase* ConfigField = FindFProperty<FObjectPropertyBase>(
        AWorldSettings::StaticClass(), TEXT("NavigationSystemConfig"));
    FBoolProperty* EnableField = FindFProperty<FBoolProperty>(
        AWorldSettings::StaticClass(), TEXT("bEnableNavigationSystem"));
    FBoolProperty* StrictField = FindFProperty<FBoolProperty>(
        UNavigationSystemModuleConfig::StaticClass(), TEXT("bStrictlyStatic"));
    UNavigationSystemConfig* OldConfig = ConfigField
        ? Cast<UNavigationSystemConfig>(ConfigField->GetObjectPropertyValue_InContainer(Settings))
        : nullptr;
    if (!ConfigField || ConfigField->PropertyClass != UNavigationSystemConfig::StaticClass() || !EnableField ||
        !StrictField ||
        (OldConfig && OldConfig->GetClass() != UNavigationSystemConfig::StaticClass() &&
            !OldConfig->IsA<UNavigationSystemModuleConfig>()))
    {
        Error = TEXT(
            "The authored level's navigation config cannot be preserved by the game's strictly-static config adapter.");
        return false;
    }

    if (Settings->GetClass() == AWorldSettings::StaticClass())
    {
        AWorldSettings* Previous = Settings;
        const FName Name = Previous->GetFName();
        if (!Previous->Rename(*(TEXT("KadanReplacedWorldSettings_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                World->PersistentLevel, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
        {
            Error = TEXT("The private world settings name could not be released.");
            return false;
        }
        FActorSpawnParameters Spawn;
        Spawn.Name = Name;
        Spawn.OverrideLevel = World->PersistentLevel;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Settings = World->SpawnActor<AWorldSettings>(GameSettings, Spawn);
        if (!Settings)
        {
            Error = TEXT("The game's actual WorldSettings actor could not be created on the private level.");
            return false;
        }
        // This is the engine's RepairWorldSettings migration, scoped to this
        // clone. Full values preserve authored base settings, including defaults
        // that differ between Engine.WorldSettings and the game subclass.
        Settings->UnregisterAllComponents();
        UEngine::FCopyPropertiesForUnrelatedObjectsParams Copy;
        Copy.bDoDelta = false;
        Copy.bCopyDeprecatedProperties = true;
        Copy.bReplaceObjectClassReferences = false;
        Copy.bNotifyObjectReplacement = false;
        UEngine::CopyPropertiesForUnrelatedObjects(Previous, Settings, Copy);
        World->PersistentLevel->SetWorldSettings(Settings);
        TMap<UObject*, UObject*> Replacements;
        Replacements.Add(Previous, Settings);
        FArchiveReplaceObjectRef<UObject> Remap(World, Replacements, false, true, true);
        // DestroyActor protects the current settings actor. SetWorldSettings
        // assigns its cached pointer after its first Destroy attempt, so finish
        // retirement only after installation and private-reference replacement.
        if (IsValid(Previous) && !Previous->Destroy())
        {
            Error = TEXT("The replaced private world settings actor could not be retired.");
            return false;
        }
        for (int32 At = 1; At < World->PersistentLevel->Actors.Num(); ++At)
            if (World->PersistentLevel->Actors[At] == Settings)
                World->PersistentLevel->Actors[At] = nullptr;
    }
    // Any settings actor left beside the current one (a level the editor repaired on opening keeps the old
    // actor and saves it again) is retired; references to it now name the current settings.
    for (AWorldSettings* Stale : OtherSettings(Settings))
        if (!Retire(Stale, Settings))
        {
            Error = TEXT("A leftover private world settings actor could not be retired: ") + Stale->GetName();
            return false;
        }

    UNavigationSystemConfig* Config = Cast<UNavigationSystemConfig>(
        ConfigField->GetObjectPropertyValue_InContainer(Settings));
    if (!Config || !Config->IsA<UNavigationSystemModuleConfig>() || Config->GetOuter() != Settings)
    {
        const FName Name = OldConfig ? OldConfig->GetFName() : FName(TEXT("NavigationSystemConfig"));
        if (UObject* Occupied = FindObject<UObject>(Settings, *Name.ToString()))
            if (!Occupied->Rename(
                    *(TEXT("KadanReplacedNavigationConfig_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    Settings, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
            {
                Error = TEXT("The private navigation config name could not be released.");
                return false;
            }
        if (OldConfig && OldConfig->IsA<UNavigationSystemModuleConfig>())
            Config = Cast<UNavigationSystemConfig>(StaticDuplicateObject(OldConfig, Settings, Name));
        else
        {
            Config = NewObject<UNavigationSystemModuleConfig>(Settings, Name);
            if (OldConfig)
            {
                UEngine::FCopyPropertiesForUnrelatedObjectsParams Copy;
                Copy.bDoDelta = false;
                Copy.bNotifyObjectReplacement = false;
                UEngine::CopyPropertiesForUnrelatedObjects(OldConfig, Config, Copy);
            }
        }
        if (!Config)
        {
            Error = TEXT("An owned strictly-static navigation config could not be created.");
            return false;
        }
        ConfigField->SetObjectPropertyValue_InContainer(Settings, Config);
    }
    Config->NavigationSystemClass = FSoftClassPath(GameNavigation);
    StrictField->SetPropertyValue_InContainer(Config, true);
    EnableField->SetPropertyValue_InContainer(Settings, false);
    Settings->SetNavigationSystemConfigOverride(nullptr);
    int32 LiveSettings = 0;
    FString SettingsActors;
    for (int32 At = 0; At < World->PersistentLevel->Actors.Num(); ++At)
        if (AActor* Actor = World->PersistentLevel->Actors[At])
            if (IsValid(Actor) && Actor->IsA<AWorldSettings>())
            {
                ++LiveSettings;
                SettingsActors += FString::Printf(
                    TEXT(" [%d]%s:%s"), At, *Actor->GetName(), *Actor->GetClass()->GetName());
            }
    if (LiveSettings != 1 || World->GetWorldSettings() != Settings || World->PersistentLevel->Actors.Num() == 0 ||
        World->PersistentLevel->Actors[0] != Settings || Settings->GetClass() != GameSettings ||
        Config->GetOuter() != Settings ||
        Config->NavigationSystemClass.ToString() != TEXT(P4_GAME_SCRIPT_PACKAGE ".CarrotNavigationSystem") ||
        !StrictField->GetPropertyValue_InContainer(Config) || EnableField->GetPropertyValue_InContainer(Settings))
    {
        Error = FString::Printf(
            TEXT(
                "Prepared private world settings failed native class, ownership or navigation readback (settings actors %d%s, current %d, first %d, class %s, config owned %d, navigation %s, strict %d, enabled %d)."),
            LiveSettings, *SettingsActors, World->GetWorldSettings() == Settings,
            World->PersistentLevel->Actors.Num() && World->PersistentLevel->Actors[0] == Settings,
            *Settings->GetClass()->GetName(), Config->GetOuter() == Settings, *Config->NavigationSystemClass.ToString(),
            StrictField->GetPropertyValue_InContainer(Config), EnableField->GetPropertyValue_InContainer(Settings));
        return false;
    }
    return true;
}

struct FP4GamePreviewSceneAssets::FImpl
{
    TArray<TStrongObjectPtr<UPackage>> Packages;
    TArray<TStrongObjectPtr<UObject>> Objects;
    TArray<UObject*> Registered;
    TStrongObjectPtr<UObject> Parent;
    TStrongObjectPtr<UPackage> ParentPackage;
    UObject* Scene = nullptr;
    UObject* Counts = nullptr;
    bool MadeParent = false;
};

FP4GamePreviewSceneAssets::FP4GamePreviewSceneAssets() : Impl(MakeUnique<FImpl>())
{}

FP4GamePreviewSceneAssets::~FP4GamePreviewSceneAssets()
{
    for (UObject* Asset : Impl->Registered)
    {
        if (const FSceneProof* Proof = SceneProofs.Find(Asset))
            if (Proof->Owner == this)
                SceneProofs.Remove(Asset);
        FAssetRegistryModule::AssetDeleted(Asset);
    }
    for (const auto& Object : Impl->Objects)
    {
        Object->ClearFlags(RF_Public | RF_Standalone);
        Object->SetFlags(RF_Transient);
        Object->MarkPendingKill();
    }
    Impl->Objects.Reset();
    for (const auto& Package : Impl->Packages)
    {
        Package->SetDirtyFlag(false);
        Package->Rename(*(TEXT("/Temp/KadanPreviewReleased/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
            nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Package->MarkPendingKill();
    }
    if (Impl->MadeParent && Impl->Parent.IsValid())
    {
        // The placeholder must not keep the game's SD_Area path occupied after preparation.
        UObject* Parent = Impl->Parent.Get();
        Parent->ClearFlags(RF_Public | RF_Standalone);
        Parent->SetFlags(RF_Transient);
        if (Impl->ParentPackage.IsValid())
        {
            Impl->ParentPackage->SetDirtyFlag(false);
            Impl->ParentPackage->Rename(
                *(TEXT("/Temp/KadanPreviewReleased/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr,
                REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
            Impl->ParentPackage->MarkPendingKill();
        }
        else
            Parent->Rename(
                nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Parent->MarkPendingKill();
    }
}

TArray<FString> P4GamePreviewExploreCategories(const UEnum* Enum)
{
    TArray<FString> Categories;
    // The last entry is the engine's own _MAX.
    for (int32 Index = 0; Enum && Index < Enum->NumEnums() - 1; ++Index)
    {
        const FString Name = Enum->GetNameStringByIndex(Index);
        if (Name == TEXT("Max") || Name == TEXT("None") || Enum->GetValueByIndex(Index) != int64(Index))
            break;
        Categories.Add(Name);
    }
    return Categories;
}

bool FP4GamePreviewSceneAssets::Create(const FString& Root, UWorld* World, const FP4GamePreviewSceneRecipe& Recipe,
    FString& Error, const TArray<FString>& LoadAssets)
{
    if (!IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("The private preview world is unavailable.");
        return false;
    }
    return Create(Root, World->GetPathName(), Recipe, Error, LoadAssets);
}

bool FP4GamePreviewSceneAssets::Create(const FString& Root, const FString& WorldObjectPath,
    const FP4GamePreviewSceneRecipe& Recipe, FString& Error, const TArray<FString>& LoadAssets)
{
    Error.Reset();
    const FString WorldPackage = FPackageName::ObjectPathToPackageName(WorldObjectPath);
    const FString& WorldName = Recipe.World;
    if (!IsInGameThread() || Impl->Objects.Num() || WorldName.IsEmpty() ||
        FPackageName::GetLongPackageAssetName(WorldPackage) != WorldName ||
        WorldObjectPath != WorldPackage + TEXT(".") + WorldName || !GeneratedPreviewPackage(Root + TEXT("/Preview")) ||
        !WorldPackage.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive))
    {
        Error = TEXT("Reserve a free game area before preparing its private preview scene and world.");
        return false;
    }
    UClass* SceneClass = LoadObject<UClass>(nullptr, *Recipe.SceneClass);
    UClass* CountsClass = LoadObject<UClass>(nullptr, *Recipe.CountsClass);
    if (!SceneClass || !CountsClass)
    {
        Error = TEXT("The game scene and actor-count classes are required for scratch level Play.");
        return false;
    }
    const FString ParentPath = Recipe.ParentScene;
    const FString ParentPackageName = FPackageName::ObjectPathToPackageName(ParentPath);
    const FString ParentName = FPackageName::ObjectPathToObjectName(ParentPath);
    if (!FPackageName::IsValidLongPackageName(ParentPackageName) || ParentName.IsEmpty() || Recipe.Scene.IsEmpty() ||
        Recipe.Counts.IsEmpty())
    {
        Error = TEXT("The preview template does not name the scene's parent, scene and placement counts.");
        return false;
    }
    UObject* Parent = FindObject<UObject>(nullptr, *ParentPath);
    if (!Parent && FPackageName::DoesPackageExist(ParentPackageName))
        Parent = LoadObject<UObject>(nullptr, *ParentPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Parent)
    {
        UPackage* Package = FindPackage(nullptr, *ParentPackageName);
        if (!Package)
        {
            Package = CreatePackage(*ParentPackageName);
            Impl->ParentPackage.Reset(Package);
        }
        Parent = NewObject<UObject>(Package, SceneClass, FName(*ParentName), RF_Public | RF_Standalone);
        Impl->MadeParent = true;
    }
    if (!Parent || Parent->GetClass() != SceneClass)
    {
        Error = TEXT("The game's parent area scene reference is unavailable or has the wrong class.");
        return false;
    }
    Impl->Parent.Reset(Parent);
    const auto MakeAsset = [&](const FString& Name, UClass* Class) -> UObject*
    {
        const FString PackageName = Root / TEXT("PreviewScene") / Name;
        if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
            return nullptr;
        UPackage* Package = CreatePackage(*PackageName);
        Impl->Packages.Emplace(Package);
        UObject* Object = NewObject<UObject>(Package, Class, FName(*Name), RF_Public | RF_Standalone);
        if (Object)
            Impl->Objects.Emplace(Object);
        return Object;
    };
    Impl->Counts = MakeAsset(Recipe.Counts, CountsClass);
    Impl->Scene = MakeAsset(Recipe.Scene, SceneClass);
    if (!Impl->Counts || !Impl->Scene)
    {
        Error = TEXT("The generated preview scene or actor-count package is already occupied.");
        return false;
    }
    FStructProperty* CountInfo = FindFProperty<FStructProperty>(CountsClass, TEXT("ActorPlacedNumInfo"));
    FMapProperty* CountMap = CountInfo ? FindFProperty<FMapProperty>(CountInfo->Struct, TEXT("ExploreRateActorNum"))
                                       : nullptr;
    FUInt64Property* CountVersion = CountInfo ? FindFProperty<FUInt64Property>(CountInfo->Struct, TEXT("AssetVersion"))
                                              : nullptr;
    FEnumProperty* EnumKey = CountMap ? CastField<FEnumProperty>(CountMap->KeyProp) : nullptr;
    FByteProperty* ByteKey = CountMap ? CastField<FByteProperty>(CountMap->KeyProp) : nullptr;
    UEnum* CountEnum = EnumKey ? EnumKey->GetEnum() : ByteKey ? ByteKey->Enum : nullptr;
    FNumericProperty* KeyNumber = EnumKey ? EnumKey->GetUnderlyingProperty() : ByteKey;
    FIntProperty* CountNumber = CountMap ? CastField<FIntProperty>(CountMap->ValueProp) : nullptr;
    const TArray<FString> CategoryNames = P4GamePreviewExploreCategories(CountEnum);
    if (!CountInfo || !CountMap || !CountVersion || !KeyNumber || !CountNumber || CategoryNames.Num() == 0 ||
        CategoryNames.Num() > 32 || Recipe.CountsVersion == 0 || Recipe.CountsVersion > (uint64(1) << 53))
    {
        Error = TEXT("The game's actor-count enum map or exact asset-version field is unavailable.");
        return false;
    }
    // The game indexes each category without checking whether its key exists. An empty authored map
    // still needs every category at zero, as the game's own counts assets have; an empty TMap crashes
    // on arrival.
    auto Counts = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (const FString& Name : CategoryNames)
        Entries.Add(MakeShared<FJsonValueArray>(
            TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueString>(Name), MakeShared<FJsonValueNumber>(0)}));
    auto CountsInfo = MakeShared<FJsonObject>();
    CountsInfo->SetNumberField(TEXT("AssetVersion"), double(Recipe.CountsVersion));
    CountsInfo->SetArrayField(TEXT("ExploreRateActorNum"), Entries);
    Counts->SetObjectField(TEXT("ActorPlacedNumInfo"), CountsInfo);
    FString CountsValues;
    FJsonSerializer::Serialize(Counts, TJsonWriterFactory<>::Create(&CountsValues));
    auto Values = MakeShared<FJsonObject>();
    Values->SetStringField(TEXT("ParentScene"), ParentPath);
    Values->SetStringField(TEXT("OpenLevel"), WorldObjectPath);
    auto Info = MakeShared<FJsonObject>();
    Info->SetStringField(TEXT("ActorPlacedNumAsset"), Impl->Counts->GetPathName());
    Values->SetObjectField(TEXT("GameSceneInfo"), Info);
    FString SceneValues;
    FJsonSerializer::Serialize(Values, TJsonWriterFactory<>::Create(&SceneValues));
    if (!UP4AuthoringTools::NativeApplyAssetValues(Impl->Counts, CountsValues) ||
        !UP4AuthoringTools::NativeApplyAssetValues(Impl->Scene, SceneValues))
    {
        Error = TEXT("The game's native scene or actor-count properties could not be prepared for scratch level Play.");
        return false;
    }
    void* CountData = CountInfo->ContainerPtrToValuePtr<void>(Impl->Counts);
    FScriptMapHelper NativeCounts(CountMap, CountMap->ContainerPtrToValuePtr<void>(CountData));
    const int32 CategoryCount = CategoryNames.Num();
    if (NativeCounts.Num() != CategoryCount ||
        CountVersion->GetPropertyValue_InContainer(CountData) != Recipe.CountsVersion)
    {
        Error = TEXT("A scratch preview requires the exact game asset version and every actor-count category.");
        return false;
    }
    uint64 Categories = 0;
    for (int32 Index = 0; Index < NativeCounts.GetMaxIndex(); ++Index)
    {
        if (!NativeCounts.IsValidIndex(Index))
            continue;
        const uint64 Category = KeyNumber->GetUnsignedIntPropertyValue(NativeCounts.GetKeyPtr(Index));
        if (Category >= uint64(CategoryCount) ||
            CountEnum->GetValueByNameString(CategoryNames[int32(Category)]) != int64(Category) ||
            CountNumber->GetPropertyValue(NativeCounts.GetValuePtr(Index)) != 0 ||
            (Categories & (uint64(1) << Category)))
        {
            Error = TEXT("The scratch preview's native actor-count map lost a required category or zero value.");
            return false;
        }
        Categories |= uint64(1) << Category;
    }
    if (Categories != (uint64(1) << CategoryCount) - 1)
    {
        Error = TEXT("The scratch preview's native actor-count map is incomplete.");
        return false;
    }
    if (LoadAssets.Num() > 32)
    {
        Error = TEXT("The private preview scene's preload list exceeds its bounded limit.");
        return false;
    }
    if (LoadAssets.Num())
    {
        FArrayProperty* Property = FindFProperty<FArrayProperty>(SceneClass, TEXT("LoadAssets"));
        FSoftObjectProperty* Item = Property ? CastField<FSoftObjectProperty>(Property->Inner) : nullptr;
        if (!Item)
        {
            Error = TEXT("The game's scene LoadAssets field is not an available native soft-object array.");
            return false;
        }
        FScriptArrayHelper LoadValues(Property, Property->ContainerPtrToValuePtr<void>(Impl->Scene));
        LoadValues.EmptyValues();
        for (const FString& Path : LoadAssets)
        {
            const FSoftObjectPath ObjectPath(Path);
            if (!ObjectPath.IsValid() || ObjectPath.IsSubobject() ||
                !ObjectPath.GetLongPackageName().StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive))
            {
                Error = TEXT("A preview scene may preload only its own generated bundle assets: ") + Path;
                return false;
            }
            const int32 Index = LoadValues.AddValue();
            Item->SetPropertyValue(LoadValues.GetRawPtr(Index), FSoftObjectPtr(ObjectPath));
            if (Item->GetPropertyValue(LoadValues.GetRawPtr(Index)).ToSoftObjectPath() != ObjectPath)
            {
                Error = TEXT("The preview scene's native preload reference did not retain its object path.");
                return false;
            }
        }
    }
    for (const auto& Object : Impl->Objects)
    {
        SceneProofs.Add(Object.Get(), {this, Object->GetPathName(), Object->GetClass()->GetPathName()});
        FAssetRegistryModule::AssetCreated(Object.Get());
        Impl->Registered.Add(Object.Get());
    }
    return true;
}

FString FP4GamePreviewSceneAssets::SceneObjectPath() const
{
    return Impl->Scene ? Impl->Scene->GetPathName() : FString();
}

FString FP4GamePreviewSceneAssets::CountsObjectPath() const
{
    return Impl->Counts ? Impl->Counts->GetPathName() : FString();
}

TArray<UObject*> FP4GamePreviewSceneAssets::Assets() const
{
    return Impl->Registered;
}

bool P4IsGamePreviewSceneAsset(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    const FSceneProof* Proof = SceneProofs.Find(Asset);
    return Proof && Proof->ObjectPath == Asset->GetPathName() && Proof->ClassPath == Asset->GetClass()->GetPathName() &&
        GeneratedPreviewPackage(Asset->GetOutermost()->GetName());
}

bool P4IsGamePreviewScratchWorld(UWorld* World)
{
    return IsInGameThread() && IsValid(World) && World->PersistentLevel &&
        P4QueryOriginalAsset(World).State == EP4OriginalState::NotImported;
}

bool P4GatherGamePreviewWorld(UWorld* World, FP4GamePreviewWorldPlan& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("Open an editor level before preparing its game preview.");
        return false;
    }
    const FP4OriginalStatus WorldStatus = P4QueryOriginalAsset(World);
    if (WorldStatus.IsOriginal())
        return true;
    if (WorldStatus.State != EP4OriginalState::NotImported)
    {
        Error = TEXT("Resolve this level's original source record before Play. ") + WorldStatus.Error;
        return false;
    }
    Out.Scratch = true;
    Out.WorldSourceObjectPath = World->GetPathName();
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FString> Pending{World->GetOutermost()->GetName()};
    TSet<FString> Seen;
    TMap<FString, FString> RequiredBy;
    RequiredBy.Add(World->GetOutermost()->GetName(), World->GetPathName());
    TSet<UObject*> Derived;
    WorldBuildData(World, Derived);
    for (UObject* Object : Derived)
    {
        const FString Package = Object->GetOutermost()->GetName();
        Out.DerivedPackages.AddUnique(Package);
        Pending.AddUnique(Package);
        RequiredBy.Add(Package, World->GetPathName() + TEXT(" level build data"));
    }
    TArray<TStrongObjectPtr<UObject>> Held;
    for (int32 At = 0; At < Pending.Num(); ++At)
    {
        const FString Package = Pending[At];
        if (Seen.Contains(Package) || Package.StartsWith(TEXT("/Script/")))
            continue;
        // Editor-created material instances and other live caches can live in
        // the native transient package without carrying RF_Transient themselves.
        // That singleton is not a package that can be cloned or loaded in game.
        // Only omit this exact package here; the final native cook/reference
        // census still refuses any transient reference that survives cooking.
        if (Package != World->GetOutermost()->GetName() && FindPackage(nullptr, *Package) == GetTransientPackage())
            continue;
        Seen.Add(Package);
        if (Seen.Num() > MaxPackages)
        {
            Error = TEXT("The level preview dependency closure exceeds 4096 packages.");
            return false;
        }
        UObject* Asset = Package == World->GetOutermost()->GetName() ? World : PackageAsset(Package);
        if (!IsValid(Asset))
        {
            Error = TEXT("A level preview dependency could not be loaded: ") + Package + TEXT(" (required by ") +
                RequiredBy.FindRef(Package) + TEXT(").");
            return false;
        }
        Held.Emplace(Asset);
        if (GameSource(Asset))
        {
            Out.GamePackages.AddUnique(Package);
            continue;
        }
        const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
        if (Status.State != EP4OriginalState::NotImported)
        {
            Error = Package + TEXT(" has unresolved original source metadata. ") + Status.Error;
            return false;
        }
        if (Package.StartsWith(TEXT("/Engine/")))
        {
            if (!EngineFile(Asset))
            {
                Error = TEXT("An engine preview dependency is not an installed engine asset: ") + Package;
                return false;
            }
            Out.EnginePackages.Add(Package);
        }
        else if (Asset != World && !Derived.Contains(Asset) && !Asset->IsA<UWorld>())
        {
            FString Why;
            if (!P4IsCreatorAsset(Asset, &Why))
            {
                Error = Package + TEXT(" is used by ") + RequiredBy.FindRef(Package) +
                    TEXT(" and can't go into Play. ") + Why;
                return false;
            }
        }
        if (UWorld* Level = Cast<UWorld>(Asset))
        {
            Error = P4GamePreviewGameplayActorIssue(Level);
            if (!Error.IsEmpty())
                return false;
        }
        Out.SourcePackages.Add(Package);
        TArray<FName> Dependencies;
        Registry.GetDependencies(FName(*Package), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        const auto AddDependency = [&](const FString& Dependency)
        {
            Pending.AddUnique(Dependency);
            if (!RequiredBy.Contains(Dependency))
                RequiredBy.Add(Dependency, Asset->GetPathName());
        };
        for (FName Dependency : Dependencies)
            AddDependency(Dependency.ToString());
        for (const FString& Dependency : UP4AuthoringTools::NativeAssetDependencies(Asset))
            AddDependency(Dependency);
        if (Pending.Num() > MaxPackages * 4)
        {
            Error = TEXT("The level preview dependency queue exceeds its bounded limit.");
            return false;
        }
    }
    Out.SourcePackages.Sort();
    Out.EnginePackages.Sort();
    Out.DerivedPackages.Sort();
    Out.GamePackages.Sort();
    return true;
}

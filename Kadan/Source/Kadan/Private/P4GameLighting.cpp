#include "P4GameLighting.h"
#include "P4ExportProgress.h"
#include "Components/SkyLightComponent.h"
#include "P4AddedLightingAttachment.h"
#include "P4ExportContainer.h"
#include "P4OriginalRecord.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureCube.h"
#include "GameFramework/WorldSettings.h"
#include "StaticMeshResources.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Components/LightComponent.h"
#include "Components/ModelComponent.h"
#include "ComponentRecreateRenderStateContext.h"
#include "CoreGlobals.h"
#include "LightingBuildOptions.h"
#include "ShadowMap.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "RenderingThread.h"
#include "Async/TaskGraphInterfaces.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

bool P4IsRetainedLightingObject(ULevel* Level, UObject* Object)
{
    if (!IsValid(Level) || !IsValid(Object) || Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        return false;
    UActorComponent* Component = Cast<UActorComponent>(Object);
    AActor* Actor = Component ? Component->GetOwner() : Cast<AActor>(Object);
    if (!IsValid(Actor) || Actor->IsPendingKillPending() ||
        Actor->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || Actor->GetLevel() != Level ||
        !Level->Actors.Contains(Actor))
        return false;
    return !Component ||
        (!Component->IsBeingDestroyed() && Component->IsInOuter(Actor) && Actor->GetComponents().Contains(Component));
}

bool P4ValidateRebuiltMeshLighting(
    UMapBuildDataRegistry* Registry, const TArray<FGuid>& OriginalIds, bool ComponentPresent, FString& Error)
{
    if (!Registry)
    {
        Error = TEXT("The lighting build produced no registry.");
        return false;
    }
    for (const FGuid& Guid : OriginalIds)
    {
        if (!Guid.IsValid())
            continue;
        const bool HasData = Registry->GetMeshBuildDataDuringBuild(Guid) != nullptr;
        if (ComponentPresent && !HasData)
        {
            Error = TEXT("The lighting build did not retain every original mesh LOD's lighting identity.");
            return false;
        }
        if (!ComponentPresent && HasData)
        {
            Error = TEXT("The lighting build retained old lighting for a removed actor.");
            return false;
        }
    }
    Error.Reset();
    return true;
}

bool P4ValidateRebuiltLight(
    UMapBuildDataRegistry* Registry, const FGuid& OriginalId, bool ComponentPresent, FString& Error)
{
    if (!Registry || !OriginalId.IsValid())
    {
        Error = TEXT("The original light has no usable lighting identity or rebuilt registry.");
        return false;
    }
    const bool HasData = Registry->GetLightBuildData(OriginalId) != nullptr;
    if (ComponentPresent != HasData)
    {
        Error = ComponentPresent ? TEXT("The lighting build did not retain an original light's identity.")
                                 : TEXT("The lighting build retained old data for a removed light.");
        return false;
    }
    Error.Reset();
    return true;
}

namespace
{
// The game levels of the open map with the lighting identities the import kept, and those identities put
// back on their components and lights, so that a build's registry can be used at the game's paths without
// shipping a copy of the level.
bool PrepareGameLevels(UWorld* PersistentMap, const FP4ExportGameInventory& Game,
    TMap<ULevel*, FP4OriginalLevelLighting>& Original, FString& Error)
{
    for (ULevel* Level : PersistentMap->GetLevels())
    {
        if (!Level || !Level->bIsVisible)
            continue;
        UWorld* World = Cast<UWorld>(Level->GetOuter());
        FP4OriginalLevelLighting Lighting;
        if (!World)
        {
            Error = TEXT("A visible map level has no world.");
            return false;
        }
        if (!Game.Contains(FName(*World->GetOutermost()->GetName())))
        {
            if (P4QueryOriginalAsset(World).IsOriginal())
            {
                Error = TEXT("An imported game level is absent from the original game package list.");
                return false;
            }
            continue; // Creator levels participate in the build without claiming game paths.
        }
        if (!P4ReadOriginalLevelLighting(World, Lighting, Error))
            return false;
        if (Level->ModelComponents.Num())
        {
            Error = TEXT("This game level has BSP lighting identities that Kadan cannot yet preserve during a rebake.");
            return false;
        }
        for (AActor* Actor : Level->Actors)
        {
            if (!P4IsRetainedLightingObject(Level, Actor))
                continue;
            TInlineComponentArray<UActorComponent*> Parts(Actor);
            for (UActorComponent* Part : Parts)
            {
                if (!P4IsRetainedLightingObject(Level, Part))
                    continue;
                // A sky light's build data is not kept under its own identity (it goes into the level's
                // sky and volumetric lighting), so it needs none to be rebaked. The game's has none.
                if (const ULightComponentBase* Light = Cast<ULightComponentBase>(Part))
                    if ((Light->HasStaticLighting() || Light->HasStaticShadowing()) &&
                        !Light->IsA<USkyLightComponent>())
                    {
                        const FString Key = Light->GetPathName(World->GetOutermost()).Replace(TEXT(":"), TEXT("."));
                        const FGuid* Guid = Lighting.Lights.Find(Key);
                        if (!Light->IsA<ULightComponent>() || !Guid || !Guid->IsValid())
                        {
                            Error =
                                TEXT(
                                    "An original static light lacks a supported serialized lighting identity. Import its level again before rebaking (") +
                                Key + TEXT(", ") + Light->GetClass()->GetName() +
                                (Guid ? TEXT(", identity not valid") : TEXT(", no identity recorded")) + TEXT(").");
                            return false;
                        }
                    }
            }
        }
        Original.Add(Level, MoveTemp(Lighting));
    }
    // The runtime level retains the game's GUIDs. Restore them before baking so
    // the generated registry can be used without shipping a copy of that level.
    FGlobalComponentRecreateRenderStateContext RenderStates;
    for (auto& Entry : Original)
    {
        UWorld* World = CastChecked<UWorld>(Entry.Key->GetOuter());
        for (const auto& Component : Entry.Value.Components)
        {
            FString Relative = Component.Key;
            const FString Prefix = World->GetName() + TEXT(".") + Entry.Key->GetName() + TEXT(".");
            if (!Relative.StartsWith(Prefix))
            {
                Error = TEXT("The original lit component is outside its level.");
                return false;
            }
            Relative = Relative.Mid(Prefix.Len());
            UStaticMeshComponent* Part = FindObject<UStaticMeshComponent>(Entry.Key, *Relative);
            if (!P4IsRetainedLightingObject(Entry.Key, Part))
            {
                FString Actor;
                Relative.Split(TEXT("."), &Actor, nullptr);
                if (P4IsRetainedLightingObject(Entry.Key, FindObject<AActor>(Entry.Key, *Actor)))
                {
                    Error = TEXT("An original lit component was lost during import. Its lighting cannot be rebuilt.");
                    return false;
                }
                continue; // The actor was removed by the creator.
            }
            Part->SetLODDataCount(Component.Value.Num(), Component.Value.Num());
            for (int32 Lod = 0; Lod < Component.Value.Num(); ++Lod)
                Part->LODData[Lod].MapBuildDataId = Component.Value[Lod];
        }
        for (const auto& Light : Entry.Value.Lights)
        {
            if (!Light.Value.IsValid())
                continue;
            const FString Prefix = World->GetName() + TEXT(".") + Entry.Key->GetName() + TEXT(".");
            if (!Light.Key.StartsWith(Prefix))
            {
                Error = TEXT("The original light is outside its level.");
                return false;
            }
            const FString Relative = Light.Key.Mid(Prefix.Len());
            UObject* Found = FindObject<UObject>(Entry.Key, *Relative);
            // A sky light keeps no build data under its own identity, so it needs none put back.
            if (Found && Found->IsA<USkyLightComponent>())
                continue;
            ULightComponent* Part = Cast<ULightComponent>(Found);
            if (!P4IsRetainedLightingObject(Entry.Key, Part))
            {
                FString Actor;
                Relative.Split(TEXT("."), &Actor, nullptr);
                if (P4IsRetainedLightingObject(Entry.Key, FindObject<AActor>(Entry.Key, *Actor)))
                {
                    Error = TEXT("An original light component was lost or has an unsupported lighting type (") +
                        Light.Key + TEXT(").");
                    return false;
                }
                continue;
            }
            Part->LightGuid = Light.Value;
        }
        if (Entry.Key->MapBuildData)
        {
            Entry.Key->MapBuildData->InvalidateReflectionCaptures();
            Entry.Key->MapBuildData->SetupLightmapResourceClusters();
        }
    }
    return true;
}
} //namespace

void P4GiveMeshesLightingIds(UWorld* World)
{
    if (!World)
        return;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMesh* Mesh = It->GetStaticMesh();
        if (!Mesh || It->GetWorld() != World || Mesh->LightingGuid.IsValid())
            continue;
        FString Key = Mesh->GetPathName();
        if (const FStaticMeshRenderData* Render = Mesh->RenderData.Get())
            for (const FStaticMeshLODResources& Lod : Render->LODResources)
                Key += FString::Printf(TEXT("|%d,%d"), Lod.Sections.Num(), Lod.GetNumVertices());
        FGuid Id;
        if (FGuid::Parse(FMD5::HashAnsiString(*Key), Id) && Id.IsValid())
            Mesh->LightingGuid = Id;
    }
}

bool P4RunLightingBuild(const FLightingBuildOptions& Options, FString& Error)
{
    P4GiveMeshesLightingIds(GEditor->GetEditorWorldContext().World());
    bool Kept = false, Failed = false;
    const FDelegateHandle KeptHandle = FEditorDelegates::OnLightingBuildKept.AddLambda([&] { Kept = true; });
    const FDelegateHandle FailedHandle = FEditorDelegates::OnLightingBuildFailed.AddLambda([&] { Failed = true; });
    ULevelEditorMiscSettings* Settings = GetMutableDefault<ULevelEditorMiscSettings>();
    const bool AutoApply = Settings->bAutoApplyLightingEnable;
    Settings->bAutoApplyLightingEnable = true;
    // A build of some of the map's levels first asks whether to build at all. The export has decided
    // that already, so the question is not asked, and the person's own answer is put back afterwards.
    const TCHAR* const Dialogs = TEXT("SuppressableDialogs");
    const TCHAR* const SkippedLevels = TEXT("WarnOnHiddenLevelsBeforeRebuild");
    bool Suppressed = false;
    const bool HadAnswer = GConfig->GetBool(Dialogs, SkippedLevels, Suppressed, GEditorPerProjectIni);
    GConfig->SetBool(Dialogs, SkippedLevels, true, GEditorPerProjectIni);
    ON_SCOPE_EXIT
    {
        Settings->bAutoApplyLightingEnable = AutoApply;
        FEditorDelegates::OnLightingBuildKept.Remove(KeptHandle);
        FEditorDelegates::OnLightingBuildFailed.Remove(FailedHandle);
        if (HadAnswer)
            GConfig->SetBool(Dialogs, SkippedLevels, Suppressed, GEditorPerProjectIni);
        else
            GConfig->RemoveKey(Dialogs, SkippedLevels, GEditorPerProjectIni);
    };
    // Applying a build clears the editor's undo history. The person's history is put aside while the
    // build applies and given back afterwards, so an export does not take away what they can undo.
    TStrongObjectPtr<UTransactor> History(GEditor->Trans);
    UTransBuffer* Scratch = NewObject<UTransBuffer>(GetTransientPackage());
    Scratch->Initialize(1024 * 1024);
    GEditor->Trans = Scratch;
    ON_SCOPE_EXIT
    {
        GEditor->Trans = History.Get();
    };
    FScopedSlowTask Progress(0, FText::FromString(TEXT("Building the edited map's lighting.")));
    if (!P4HasExportProgressObserver())
    {
        Progress.MakeDialog(true);
    }
    GEditor->BuildLighting(Options);
    const double Deadline = FPlatformTime::Seconds() + 1800.0;
    bool Cancelled = false;
    double CancelTime = 0;
    while (GEditor->IsLightingBuildCurrentlyRunning())
    {
        P4PulseExportProgress();
        if (Progress.ShouldCancel() || FPlatformTime::Seconds() > Deadline)
        {
            if (!Cancelled)
                CancelTime = FPlatformTime::Seconds();
            Cancelled = true;
            GEditor->SetMapBuildCancelled(true);
        }
        if (Cancelled && FPlatformTime::Seconds() > CancelTime + 30.0)
        {
            Error = TEXT(
                "The lighting build has not stopped after cancellation. Wait for it to finish before exporting again.");
            return false;
        }
        {
            // The editor applies a finished build only while no slow task is shown, so with this export's
            // progress dialog up it would wait for someone to press Apply Now. While the editor updates the
            // build, no slow task is said to be shown and none of its own dialogs open: its progress goes to
            // this dialog.
            const bool SlowTask = GIsSlowTask, Silent = GIsSilent;
            GIsSlowTask = false;
            GIsSilent = true;
            ON_SCOPE_EXIT
            {
                GIsSlowTask = SlowTask;
                GIsSilent = Silent;
            };
            GEditor->UpdateBuildLighting();
        }
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(0.01f);
    }
    FlushRenderingCommands();
    if (Cancelled || Failed || !Kept)
    {
        Error = Cancelled ? TEXT("The lighting build was cancelled before export.")
                          : TEXT("The lighting build did not produce usable data. Check the Output Log.");
        return false;
    }
    return true;
}

namespace
{
// The object a reference property of From names, loading a soft reference. Null when there is none.
UObject* Named(UObject* From, const TCHAR* Name)
{
    if (!From)
        return nullptr;
    if (const FSoftObjectProperty* Soft = FindFProperty<FSoftObjectProperty>(From->GetClass(), Name))
        return Soft->GetPropertyValue_InContainer(From).LoadSynchronous();
    if (const FObjectPropertyBase* Hard = FindFProperty<FObjectPropertyBase>(From->GetClass(), Name))
        return Hard->GetObjectPropertyValue_InContainer(From);
    return nullptr;
}

// The Lightmass world settings the game bakes the map with: those of the day scene preset its BP_EnvController
// reads (EnvParamSceneDay, then its EnvParamWorldSettings section). The game's editor keeps them only in that
// preset, so the map's own world settings hold none of them. Fields are taken by name, so a field of the game's
// that this engine lacks is left out.
bool GameLightmassSettings(UWorld* Map, FLightmassWorldInfoSettings& Out, FString& From)
{
    for (ULevel* Level : Map->GetLevels())
        for (AActor* Actor : Level ? Level->Actors : TArray<AActor*>())
        {
            if (!IsValid(Actor) || !Actor->GetClass()->GetName().Contains(TEXT("EnvController")))
                continue;
            UObject* Section = Named(Named(Actor, TEXT("EnvParamSceneDay")), TEXT("EnvParamWorldSettings"));
            const FStructProperty* Settings = Section
                ? FindFProperty<FStructProperty>(Section->GetClass(), TEXT("LightmassSettings"))
                : nullptr;
            if (!Settings)
                continue;
            const void* Values = Settings->ContainerPtrToValuePtr<void>(Section);
            for (TFieldIterator<FProperty> Field(Settings->Struct); Field; ++Field)
            {
                const FProperty* Mine = FLightmassWorldInfoSettings::StaticStruct()->FindPropertyByName(
                    Field->GetFName());
                if (Mine && Mine->GetClass() == Field->GetClass() && Mine->GetSize() == Field->GetSize() &&
                    Mine->SameType(*Field))
                    Mine->CopyCompleteValue(
                        Mine->ContainerPtrToValuePtr<void>(&Out), Field->ContainerPtrToValuePtr<void>(Values));
            }
            From = Section->GetPathName();
            return true;
        }
    return false;
}

// Whether a lighting build gives a mesh component lighting that is seen: it is registered and drawn, it can have
// static lighting, and its mesh has the lightmap coordinates it names.
bool CanHoldMeshLighting(const UStaticMeshComponent* Part)
{
    const UStaticMesh* Mesh = Part ? Part->GetStaticMesh() : nullptr;
    if (!Mesh || !Part->IsRegistered() || !Part->GetVisibleFlag() || !Part->HasValidSettingsForStaticLighting(false) ||
        !Mesh->RenderData || !Mesh->RenderData->LODResources.Num())
        return false;
    const int32 Coordinates = int32(
        Mesh->RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
    return Mesh->LightMapCoordinateIndex >= 0 && Mesh->LightMapCoordinateIndex < Coordinates;
}

// The game levels' rebuilt registries, each at the game's path, with every original identity rebuilt and
// every removed one gone.
bool CheckGameRegistries(
    const TMap<ULevel*, FP4OriginalLevelLighting>& Original, FP4RebuiltGameLighting& Out, FString& Error)
{
    for (const auto& Entry : Original)
    {
        if (Entry.Value.Registry.IsEmpty())
            continue;
        UMapBuildDataRegistry* Registry = Entry.Key->MapBuildData;
        if (!Registry || Registry->GetPathName() != Entry.Value.Registry)
        {
            Error = TEXT("The rebuilt lighting does not use the game's original registry path.");
            return false;
        }
        const UWorld* World = CastChecked<UWorld>(Entry.Key->GetOuter());
        const FString Prefix = World->GetName() + TEXT(".") + Entry.Key->GetName() + TEXT(".");
        for (const auto& Component : Entry.Value.Components)
        {
            UStaticMeshComponent* Part = FindObject<UStaticMeshComponent>(Entry.Key, *Component.Key.Mid(Prefix.Len()));
            const bool Present = P4IsRetainedLightingObject(Entry.Key, Part);
            // A mesh that is not drawn, or one the build cannot give a lightmap (without the lightmap coordinates its
            // mesh names, as a collision mesh), keeps an identity but needs no rebuilt lighting.
            if (Present && !CanHoldMeshLighting(Part))
                continue;
            if (!P4ValidateRebuiltMeshLighting(Registry, Component.Value, Present, Error))
            {
                Error.RemoveFromEnd(TEXT("."));
                Error += TEXT(" (") + Component.Key + TEXT(").");
                return false;
            }
        }
        for (const auto& Light : Entry.Value.Lights)
        {
            if (!Light.Value.IsValid())
                continue;
            UObject* Found = FindObject<UObject>(Entry.Key, *Light.Key.Mid(Prefix.Len()));
            if (Found && Found->IsA<USkyLightComponent>())
                continue;
            const bool Present = P4IsRetainedLightingObject(Entry.Key, Cast<ULightComponent>(Found));
            if (!P4ValidateRebuiltLight(Registry, Light.Value, Present, Error))
                return false;
        }
        Out.Registries.AddUnique(Registry);
    }
    if (!Out.Registries.Num())
    {
        Error = TEXT("The lighting build produced no registries at the game's paths.");
        return false;
    }
    return true;
}

// What the build made for an added level's own meshes stays in its registry. Everything else goes: the
// lighting of the level as a whole (a level loaded with a volumetric lightmap of its own gives the whole
// map that one), what a lighting scenario build kept there for the game's lights and reflection captures,
// and what it kept for the map's other levels. The level is detached from the map first, so no component
// in any scene draws with this registry while it changes.
bool KeepOwnLighting(FP4AddedLitLevel& Added, const TArray<FGuid>& MapLevels, FString& Error)
{
    ULevel* Level = Added.World->PersistentLevel;
    UMapBuildDataRegistry* Registry = Level->MapBuildData;
    if (!Registry || Registry->GetOutermost()->GetName() != Added.World->GetOutermost()->GetName() + TEXT("_BuiltData"))
    {
        Error = TEXT("The lighting build made no lighting data for the static meshes you added.");
        return false;
    }
    TArray<FGuid> Levels = MapLevels;
    Levels.AddUnique(Level->LevelBuildDataId);
    TSet<FGuid> Keep;
    TArray<UStaticMeshComponent*> Meshes;
    for (AActor* Actor : Level->Actors)
    {
        if (!IsValid(Actor) || !Actor->IsA<AStaticMeshActor>())
            continue;
        UStaticMeshComponent* Part = CastChecked<AStaticMeshActor>(Actor)->GetStaticMeshComponent();
        if (!Part)
            continue;
        Meshes.Add(Part);
        for (const FStaticMeshComponentLODInfo& Lod : Part->LODData)
            if (Lod.MapBuildDataId.IsValid())
                Keep.Add(Lod.MapBuildDataId);
    }
    const auto LevelWide = [&]()
    {
        for (const FGuid& Id : Levels)
            if (Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Id) ||
                Registry->GetLevelPrecomputedLightVolumeBuildData(Id))
                return true;
        return false;
    };
    const bool Had = LevelWide();
    {
        // The registry's own calls leave detaching every component that may draw with it to the caller.
        FGlobalComponentRecreateRenderStateContext Recreate;
        Registry->InvalidateStaticLighting(Added.World, false, &Keep);
        Registry->InvalidateReflectionCaptures(&Keep);
        Registry->SetupLightmapResourceClusters();
    }
    FlushRenderingCommands();
    if (LevelWide())
    {
        Error = TEXT("The lighting of the map as a whole could not be taken out of your added meshes' lighting data.");
        return false;
    }
    for (UStaticMeshComponent* Part : Meshes)
    {
        const FMeshMapBuildData* Data = Part->LODData.Num()
            ? Registry->GetMeshBuildData(Part->LODData[0].MapBuildDataId)
            : nullptr;
        if (!Data || (!Data->LightMap.IsValid() && !Data->ShadowMap.IsValid()))
        {
            Error = FString::Printf(TEXT("The lighting build did not light %s."), *Part->GetOwner()->GetName());
            return false;
        }
        Added.LitActors.Add(Part->GetOwner()->GetName());
    }
    // A reflection capture the build made textures for keeps them in the package after its data is gone.
    // Nothing names them, so they are taken out of the package before the cook.
    P4PrepareAddedLightingForCook(Registry);
    Added.Registry = Registry;
    Added.VolumeTakenOut = Had;
    return true;
}
} //namespace

void P4PrepareAddedLightingForCook(UMapBuildDataRegistry* Registry)
{
    if (!IsValid(Registry))
        return;
    TArray<UObject*> Cubes;
    ForEachObjectWithPackage(
        Registry->GetOutermost(),
        [&](UObject* Object)
        {
            if (Object->IsA<UTextureCube>())
                Cubes.Add(Object);
            return true;
        },
        true);
    for (UObject* Cube : Cubes)
    {
        UE_LOG(LogTemp, Display, TEXT("Kadan takes the unused reflection texture %s out of %s."), *Cube->GetName(),
            *Registry->GetOutermost()->GetName());
        Cube->ClearFlags(RF_Public | RF_Standalone);
        Cube->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Cube->MarkPendingKill();
    }
}

bool P4BuildEditedMapLighting(UWorld* PersistentMap, const FP4ExportGameInventory& Game, bool RebuildGame,
    TArray<FP4AddedLitLevel>& Added, FP4RebuiltGameLighting& Out, FString& Error)
{
    Out = FP4RebuiltGameLighting();
    for (FP4AddedLitLevel& Each : Added)
    {
        Each.Registry = nullptr;
        Each.LitActors.Reset();
        Each.VolumeTakenOut = false;
    }
    if (!IsInGameThread() || !GEditor || !PersistentMap || !PersistentMap->PersistentLevel ||
        GEditor->GetEditorWorldContext().World() != PersistentMap || GEditor->IsLightingBuildCurrentlyRunning())
    {
        Error = TEXT("Open the original map and finish any lighting build before exporting its lighting changes.");
        return false;
    }
    if (!RebuildGame && !Added.Num())
    {
        Error.Reset();
        return true;
    }
    for (const FP4AddedLitLevel& Each : Added)
        if (!IsValid(Each.World) || !Each.World->PersistentLevel || !Each.Sources.Num())
        {
            Error = TEXT("The level of the static meshes you added to be lit is missing.");
            return false;
        }
    // A build marks the levels and lighting packages it touches as changed. The export changes nothing of the
    // project, so every package that was saved before is left that way and the editor asks for no save.
    TArray<TWeakObjectPtr<UPackage>> Saved;
    for (TObjectIterator<UPackage> It; It; ++It)
        if (!It->IsDirty())
            Saved.Add(*It);
    ON_SCOPE_EXIT
    {
        for (const TWeakObjectPtr<UPackage>& Package : Saved)
            if (Package.IsValid() && Package->IsDirty())
                Package->SetDirtyFlag(false);
    };
    // A build numbers every primitive the editor holds for precomputed visibility (VisibilityId, which a level
    // saves). The levels keep the numbers they had, so the export leaves no changed value on the game's actors.
    TArray<TPair<TWeakObjectPtr<UPrimitiveComponent>, int32>> Visibility;
    for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
        Visibility.Emplace(*It, It->VisibilityId);
    ON_SCOPE_EXIT
    {
        FGlobalComponentRecreateRenderStateContext RenderStates;
        for (const auto& Each : Visibility)
            if (Each.Key.IsValid())
                Each.Key->VisibilityId = Each.Value;
    };
    // The build uses the Lightmass settings the game bakes this map with. Every lightmap of the game has ambient
    // occlusion and its material mask, and the game draws each lightmap with that mask, so the build makes them
    // even where the map's environment says nothing.
    AWorldSettings* WorldSettings = PersistentMap->GetWorldSettings();
    if (!WorldSettings)
    {
        Error = TEXT("The open map has no world settings for its lighting build.");
        return false;
    }
    const FLightmassWorldInfoSettings KeptLightmass = WorldSettings->LightmassSettings;
    FLightmassWorldInfoSettings Lightmass = KeptLightmass;
    if (!GameLightmassSettings(PersistentMap, Lightmass, Out.LightmassSettingsFrom))
        Out.LightmassSettingsFrom.Reset();
    Lightmass.bUseAmbientOcclusion = true;
    Lightmass.bGenerateAmbientOcclusionMaterialMask = true;
    WorldSettings->LightmassSettings = Lightmass;
    ON_SCOPE_EXIT
    {
        if (IsValid(WorldSettings))
            WorldSettings->LightmassSettings = KeptLightmass;
    };
    TMap<ULevel*, FP4OriginalLevelLighting> Original;
    if (RebuildGame && !PrepareGameLevels(PersistentMap, Game, Original, Error))
        return false;
    // Each added level joins the open map for the build, so the map's own lights, walls and floors light it.
    TArray<TUniquePtr<FP4AddedLightingAttachment>> Scopes;
    const auto DetachAll = [&](FString& Why)
    {
        bool Detached = true;
        for (FP4AddedLitLevel& Each : Added)
            if (IsValid(Each.World) && Each.World->PersistentLevel && Each.World->PersistentLevel->bIsLightingScenario)
                Each.World->PersistentLevel->SetLightingScenario(false);
        for (int32 Index = Scopes.Num() - 1; Index >= 0; --Index)
        {
            FString Each;
            if (!Scopes[Index]->Detach(Each))
            {
                Detached = false;
                if (Why.IsEmpty())
                    Why = Each;
            }
        }
        if (Detached)
            Scopes.Reset();
        return Detached;
    };
    for (FP4AddedLitLevel& Each : Added)
    {
        TUniquePtr<FP4AddedLightingAttachment> Scope = MakeUnique<FP4AddedLightingAttachment>();
        if (!Scope->Attach(PersistentMap, Each.World, Each.Sources, Error))
        {
            FString Ignored;
            DetachAll(Ignored);
            return false;
        }
        Scopes.Add(MoveTemp(Scope));
    }
    FLightingBuildOptions Options;
    Options.QualityLevel = Quality_Production;
    if (RebuildGame || !Added.Num())
    {
        // One build of every level, the game's and the added ones together.
        if (!P4RunLightingBuild(Options, Error))
        {
            FString Ignored;
            DetachAll(Ignored);
            return false;
        }
    }
    else
        // Only the added level is built, as the map's lighting scenario: the build then keeps everything it
        // makes (its meshes' lighting, and what it makes for the map and the game's lights) in the added
        // level's registry, and the game's registries are not changed.
        for (FP4AddedLitLevel& Each : Added)
        {
            ULevel* Lit = Each.World->PersistentLevel;
            Options.bOnlyBuildSelectedLevels = true;
            Options.SelectedLevels = {Lit};
            Lit->SetLightingScenario(true);
            const bool Built = P4RunLightingBuild(Options, Error);
            Lit->SetLightingScenario(false);
            if (!Built)
            {
                FString Ignored;
                DetachAll(Ignored);
                return false;
            }
        }
    if (RebuildGame && !CheckGameRegistries(Original, Out, Error))
    {
        FString Ignored;
        DetachAll(Ignored);
        return false;
    }
    TArray<FGuid> MapLevels;
    for (ULevel* Level : PersistentMap->GetLevels())
        if (Level)
            MapLevels.AddUnique(Level->LevelBuildDataId);
    if (!DetachAll(Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("The added level could not be taken out of the map after its lighting build.");
        return false;
    }
    for (FP4AddedLitLevel& Each : Added)
        if (!KeepOwnLighting(Each, MapLevels, Error))
            return false;
    Error.Reset();
    return true;
}

bool P4RebuildGameLighting(
    UWorld* PersistentMap, const FP4ExportGameInventory& Game, FP4RebuiltGameLighting& Out, FString& Error)
{
    TArray<FP4AddedLitLevel> None;
    return P4BuildEditedMapLighting(PersistentMap, Game, true, None, Out, Error);
}

void P4DiscardAddedLitLevel(FP4AddedLitLevel& Added)
{
    if (!IsInGameThread())
        return;
    TArray<UPackage*> Packages;
    if (IsValid(Added.World))
    {
        if (ULevel* Level = Added.World->PersistentLevel)
        {
            if (Level->MapBuildData)
                Packages.Add(Level->MapBuildData->GetOutermost());
            Level->MapBuildData = nullptr;
        }
        UPackage* Package = Added.World->GetOutermost();
        Added.World->ClearFlags(RF_Public | RF_Standalone);
        Added.World->DestroyWorld(false);
        Added.World->MarkPendingKill();
        Packages.Add(Package);
    }
    else if (IsValid(Added.Registry))
        Packages.Add(Added.Registry->GetOutermost());
    TArray<FName> Names;
    for (UPackage* Package : Packages)
    {
        ForEachObjectWithPackage(
            Package,
            [](UObject* Object)
            {
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->MarkPendingKill();
                return true;
            },
            true);
        Package->SetDirtyFlag(false);
        Names.Add(Package->GetFName());
    }
    Packages.Reset();
    Added.World = nullptr;
    Added.Registry = nullptr;
    Added.Sources.Reset();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    // A package the collection could not take keeps its path out of the way of the next export.
    for (const FName Name : Names)
        if (UPackage* Left = FindObjectFast<UPackage>(nullptr, Name))
            Left->Rename(
                *MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("P4DiscardedLighting")).ToString(),
                nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
}

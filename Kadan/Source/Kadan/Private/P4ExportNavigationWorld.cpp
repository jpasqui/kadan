#include "P4ExportNavigationWorld.h"
#include "P4GamePaths.h"
#include "P4NavigationLayer.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LevelUtils.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "NavigationData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

FP4ExportNavigationWorld::FP4ExportNavigationWorld()
    : Namespace(TEXT("/Temp/KadanNavigation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits))
{}

FP4ExportNavigationWorld::~FP4ExportNavigationWorld()
{
    if (Copies.Num())
    {
        UWorld* World = Copies[0].Get();
        if (Initialized)
        {
            World->CleanupWorld();
        }
        if (MadeContext && GEngine)
        {
            GEngine->DestroyWorldContext(World);
        }
        for (int32 Index = 0; Index < Copies.Num(); ++Index)
        {
            UWorld* Copy = Copies[Index].Get();
            if (!Copy)
            {
                continue;
            }
            if (Index > 0 && Copy->PersistentLevel)
            {
                Copy->PersistentLevel->OwningWorld = Copy;
            }
            Copy->ClearFlags(RF_Standalone | RF_Public);
            if (Copy->PersistentLevel)
            {
                Copy->PersistentLevel->ClearFlags(RF_Standalone | RF_Public);
            }
        }
    }
}

bool FP4ExportNavigationWorld::Gather(UWorld* Source, const FTransform& Transform, bool Hero, FString& Error)
{
    if (!IsValid(Source) || !Source->PersistentLevel)
    {
        Error = TEXT("One of this map's levels could not be loaded. Reimport the map and export again.");
        return false;
    }
    // Repeated instances need distinct reference remapping. Refuse an ambiguous
    // graph rather than taking collision from one instance's placement.
    if (Sources.Contains(Source))
    {
        Error = TEXT(
            "This map uses the same sublevel more than once. Remove the repeated streaming reference, then export again.");
        return false;
    }
    Sources.Add(Source);
    UPackage* Package = CreatePackage(*(Namespace / FString::FromInt(Copies.Num()) / Source->GetName()));
    Package->SetFlags(RF_Transient);
    FObjectDuplicationParameters Parameters(Source, Package);
    Parameters.DestName = Source->GetFName();
    Parameters.DuplicateMode = EDuplicateMode::World;
    Parameters.FlagMask = RF_AllFlags & ~(RF_Public | RF_Standalone | RF_Transactional);
    Parameters.ApplyFlags = RF_Transient;
    UWorld* Copy = Cast<UWorld>(StaticDuplicateObjectEx(Parameters));
    if (!Copy || !Copy->PersistentLevel || Copy->PersistentLevel == Source->PersistentLevel)
    {
        Error = TEXT("Unreal could not copy the map for its navigation build. Check the Output Log and export again.");
        return false;
    }
    Copies.Emplace(Copy);
    for (AActor* SourceActor : Source->PersistentLevel->Actors)
    {
        if (!IsValid(SourceActor))
        {
            continue;
        }
        AActor* CopiedActor = FindObject<AActor>(Copy->PersistentLevel, *SourceActor->GetName());
        if (IsValid(CopiedActor))
        {
            CopiedActors.Add(SourceActor, CopiedActor);
        }
    }
    Copy->SetStreamingLevels(TArray<ULevelStreaming*>());
    ULevel* Level = Copy->PersistentLevel;
    HeroLevels.Add(Level, Hero);
    if (!Transform.Equals(FTransform::Identity))
    {
        FLevelUtils::FApplyLevelTransformParams Move(Level, Transform);
        Move.bDoPostEditMove = false;
        FLevelUtils::ApplyLevelTransform(Move);
    }
    for (ULevelStreaming* Streaming : Source->GetStreamingLevels())
    {
        if (!Streaming)
        {
            continue;
        }
        const FString ChildPackage = Streaming->GetWorldAssetPackageName();
        const FString ChildName = FPackageName::GetShortName(ChildPackage);
        UWorld* Child = LoadObject<UWorld>(nullptr, *(ChildPackage + TEXT(".") + ChildName));
        const bool ChildHero = Hero || P4IsOlimarNavigationLevel(ChildPackage);
        // Loaded editor sublevels already contain their streaming transform.
        const ULevel* Loaded = Streaming->GetLoadedLevel();
        const bool AlreadyMoved = (Loaded && Loaded->bAlreadyMovedActors) ||
            (Child && Child->PersistentLevel && Child->PersistentLevel->bAlreadyMovedActors);
        const FTransform ChildTransform = AlreadyMoved ? Transform : Streaming->LevelTransform * Transform;
        if (!Gather(Child, ChildTransform, ChildHero, Error))
        {
            return false;
        }
    }
    return true;
}

UWorld* FP4ExportNavigationWorld::Load(const FString& Package, FString& Error)
{
    if (!IsInGameThread() || Copies.Num())
    {
        Error = TEXT("Unreal could not start the map's navigation build. Restart the editor and export again.");
        return nullptr;
    }
    UWorld* Source = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Package));
    if (!Gather(Source, FTransform::Identity, false, Error))
    {
        return nullptr;
    }
    // Each duplicated level remaps its own objects. Restore only cross-level
    // actor ownership, which collision actors may use. Serializing every
    // imported object to rewrite unrelated references is unsafe here.
    for (const auto& Pair : CopiedActors)
    {
        AActor* SourceOwner = Pair.Value->GetOwner();
        if (AActor** CopiedOwner = CopiedActors.Find(SourceOwner))
        {
            Pair.Value->SetOwner(*CopiedOwner);
        }
    }
    UWorld* World = Copies[0].Get();
    World->WorldType = EWorldType::Editor;
    World->SetNavigationSystem(nullptr);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    MadeContext = true;
    World->InitWorld(UWorld::InitializationValues()
            .AllowAudioPlayback(false)
            .RequiresHitProxies(false)
            .CreatePhysicsScene(true)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .ShouldSimulatePhysics(false));
    Initialized = true;
    World->SetCurrentLevel(World->PersistentLevel);
    for (int32 Index = 1; Index < Copies.Num(); ++Index)
    {
        ULevel* Level = Copies[Index]->PersistentLevel;
        Level->OwningWorld = World;
        Level->bIsVisible = false;
        World->AddToWorld(Level, FTransform::Identity, false);
    }
    World->UpdateWorldComponents(false, false);
    return World;
}

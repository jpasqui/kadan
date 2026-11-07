#include "P4AddedLightingAttachment.h"
#include "P4OriginalRecord.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Brush.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ActorComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "LightingBuildOptions.h"
#include "RenderingThread.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Class.h"

struct FP4AddedLightingState
{
    TStrongObjectPtr<UWorld> Map, Creator;
    TStrongObjectPtr<ULevel> PreviousCurrent;
    UWorld* PreviousOwner = nullptr;
    bool PreviousVisible = false, PreviousComponentsRegistered = false, Attached = false;
    TArray<TWeakObjectPtr<UActorComponent>> Suppressed, CreatorRegistered;

    bool Detach(FString& Error)
    {
        Error.Reset();
        if (!Attached)
            return true;
        if (!IsInGameThread() || (GEditor && GEditor->IsLightingBuildCurrentlyRunning()))
        {
            Error = TEXT("Wait for the lighting build to stop before restoring the added actors.");
            return false;
        }
        if (!IsValid(Map.Get()) || Map->bIsTearingDown || !IsValid(Creator.Get()) || !IsValid(Creator->PersistentLevel))
        {
            Attached = false;
            return true;
        } // Do not recreate components in an editor-destroyed scene.
        ULevel* Level = Creator->PersistentLevel;
        if (Map->GetCurrentLevel() == Level && PreviousCurrent.IsValid())
            Map->SetCurrentLevel(PreviousCurrent.Get());
        // Unlike UEditorLevelUtils::RemoveLevelFromWorld, these APIs do not
        // destroy actors, reset the undo buffer or run garbage collection.
        Map->RemoveFromWorld(Level, false);
        Map->RemoveLevel(Level);
        Level->OwningWorld = PreviousOwner;
        Level->bIsVisible = PreviousVisible;
        for (const auto& Part : CreatorRegistered)
            if (Part.IsValid() && !Part->IsRegistered())
                Part->RegisterComponentWithWorld(Creator.Get());
        Level->bAreComponentsCurrentlyRegistered = PreviousComponentsRegistered;
        for (const auto& Part : Suppressed)
            if (Part.IsValid() && !Part->IsRegistered())
                Part->RegisterComponentWithWorld(Map.Get());
        FlushRenderingCommands();
        Attached = false;
        return true;
    }
};

FP4AddedLightingAttachment::FP4AddedLightingAttachment() = default;
FP4AddedLightingAttachment::~FP4AddedLightingAttachment()
{
    if (!State || !State->Attached)
        return;
    FString Error;
    if (State->Detach(Error))
        return;
    // A bounded cancellation can return while UE still owns the job. Hold the
    // worlds and excluded components until the editor reports it has stopped;
    // neither publish data nor detach geometry out from under Lightmass.
    const TSharedPtr<FP4AddedLightingState> Pending = MoveTemp(State);
    FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Pending](float)
        {
            if (!GEditor)
                return false; // Editor shutdown destroys the owning scene.
            if (GEditor->IsLightingBuildCurrentlyRunning())
                return true;
            FString Why;
            return !Pending->Detach(Why);
        }));
}

bool FP4AddedLightingAttachment::Attach(UWorld* Map, UWorld* Creator, const TArray<AActor*>& Added, FString& Error)
{
    Error.Reset();
    if (State || !IsInGameThread() || !GEditor || GEditor->IsLightingBuildCurrentlyRunning() || !IsValid(Map) ||
        !IsValid(Creator) || Map == Creator || Map->IsGameWorld() || Creator->IsGameWorld() || !Map->PersistentLevel ||
        !Creator->PersistentLevel || !Map->Scene || Map->OriginLocation != FIntVector::ZeroValue ||
        Map->GetActiveLightingScenario() || Creator->PersistentLevel->bIsLightingScenario ||
        Creator->PersistentLevel->MapBuildData ||
        (Creator->PersistentLevel->OwningWorld && Creator->PersistentLevel->OwningWorld != Creator) ||
        Creator->GetStreamingLevels().Num() != 0 || Map->GetLevels().Contains(Creator->PersistentLevel) ||
        P4QueryOriginalAsset(Creator).IsOriginal() || Added.Num() == 0)
    {
        Error = TEXT(
            "Added lighting needs a fresh creator level and an idle original-map scene without a lighting scenario.");
        return false;
    }
    ULevel* Level = Creator->PersistentLevel;
    TSet<FName> Names;
    TArray<UActorComponent*> Suppress;
    for (AActor* Actor : Added)
    {
        if (!IsValid(Actor) || Actor->IsPendingKillPending() || Actor->GetClass() != AStaticMeshActor::StaticClass() ||
            !Map->GetLevels().Contains(Actor->GetLevel()) || !Actor->GetLevel()->Actors.Contains(Actor) ||
            Names.Contains(Actor->GetFName()))
        {
            Error = TEXT("Only the export's actual added static mesh actors can enter its lighting scope.");
            return false;
        }
        AStaticMeshActor* Copy = FindObject<AStaticMeshActor>(Level, *Actor->GetName());
        const UStaticMeshComponent* Source = CastChecked<AStaticMeshActor>(Actor)->GetStaticMeshComponent();
        const UStaticMeshComponent* Copied = IsValid(Copy) ? Copy->GetStaticMeshComponent() : nullptr;
        if (!IsValid(Copy) || Copy->GetClass() != AStaticMeshActor::StaticClass() || !Level->Actors.Contains(Copy) ||
            !Source || !Copied || Source->Mobility != EComponentMobility::Static ||
            Copied->Mobility != EComponentMobility::Static || !Source->IsRegistered() ||
            Source->GetStaticMesh() != Copied->GetStaticMesh() ||
            !Actor->GetActorTransform().Equals(Copy->GetActorTransform()) ||
            !Source->GetRelativeTransform().Equals(Copied->GetRelativeTransform()) ||
            !Source->GetComponentTransform().Equals(Copied->GetComponentTransform()) ||
            Source->CastShadow != Copied->CastShadow || Source->bCastStaticShadow != Copied->bCastStaticShadow ||
            Source->bOverrideLightMapRes != Copied->bOverrideLightMapRes ||
            Source->OverriddenLightMapRes != Copied->OverriddenLightMapRes ||
            Source->bOverrideMinLOD != Copied->bOverrideMinLOD || Source->MinLOD != Copied->MinLOD ||
            Source->ForcedLodModel != Copied->ForcedLodModel ||
            Source->GetNumMaterials() != Copied->GetNumMaterials() ||
            !FLightmassPrimitiveSettings::StaticStruct()->CompareScriptStruct(
                &Source->LightmassSettings, &Copied->LightmassSettings, 0))
        {
            Error = TEXT("The fresh lighting copy differs from its added static mesh actor.");
            return false;
        }
        for (int32 Slot = 0; Slot < Source->GetNumMaterials(); ++Slot)
            if (Source->GetMaterial(Slot) != Copied->GetMaterial(Slot))
            {
                Error = TEXT("The fresh lighting copy uses different materials from its added mesh.");
                return false;
            }
        Names.Add(Actor->GetFName());
        for (UActorComponent* Part : Actor->GetComponents())
            if (Part && Part->IsRegistered())
                Suppress.Add(Part);
    }
    for (AActor* Actor : Level->Actors)
        if (IsValid(Actor) && !Actor->IsA<AWorldSettings>() && !Actor->IsA<ADefaultPhysicsVolume>() &&
            Actor != Creator->GetDefaultBrush() && !Names.Contains(Actor->GetFName()))
        {
            Error = TEXT("The lighting level contains an actor outside the export's added mesh list.");
            return false;
        }
    State = MakeShared<FP4AddedLightingState>();
    State->Map.Reset(Map);
    State->Creator.Reset(Creator);
    State->PreviousCurrent.Reset(Map->GetCurrentLevel());
    State->PreviousOwner = Level->OwningWorld;
    State->PreviousVisible = Level->bIsVisible;
    State->PreviousComponentsRegistered = Level->bAreComponentsCurrentlyRegistered;
    for (AActor* Actor : Level->Actors)
        if (IsValid(Actor))
            for (UActorComponent* Part : Actor->GetComponents())
                if (Part && Part->IsRegistered())
                    State->CreatorRegistered.Add(Part);
    Level->ClearLevelComponents();
    for (UActorComponent* Part : Suppress)
    {
        State->Suppressed.Add(Part);
        Part->UnregisterComponent();
    }
    State->Attached = true;
    Map->AddToWorld(Level, FTransform::Identity, false);
    bool CopiesRegistered = true;
    for (const FName Name : Names)
    {
        const AStaticMeshActor* Copy = FindObject<AStaticMeshActor>(Level, *Name.ToString());
        CopiesRegistered = CopiesRegistered && IsValid(Copy) && Copy->GetStaticMeshComponent()->IsRegistered() &&
            Copy->GetWorld() == Map;
    }
    if (!CopiesRegistered || !Level->bIsVisible || Level->OwningWorld != Map || !Map->GetLevels().Contains(Level) ||
        !Map->SetCurrentLevel(Level))
    {
        FString Restore;
        State->Detach(Restore);
        Error = TEXT("The editor could not attach the creator lighting level.");
        State.Reset();
        return false;
    }
    FlushRenderingCommands();
    return true;
}

bool FP4AddedLightingAttachment::Detach(FString& Error)
{
    Error.Reset();
    return !State || State->Detach(Error);
}

bool FP4AddedLightingAttachment::BuildOptions(FLightingBuildOptions& Options, FString& Error) const
{
    Error.Reset();
    if (!State || !State->Attached || State->Map->GetCurrentLevel() != State->Creator->PersistentLevel)
    {
        Error = TEXT("The creator lighting level must be current before its build.");
        return false;
    }
    Options = FLightingBuildOptions();
    Options.QualityLevel = Quality_Production;
    Options.bOnlyBuildCurrentLevel = true;
    return true;
}

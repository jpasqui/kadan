#include "P4AuthoringRows.h"
#include "P4GamePaths.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "EditorLevelUtils.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "LevelUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/ITransaction.h"

namespace P4StreamingInternals
{
FDelegateHandle UndoDelegate;
TSet<TWeakObjectPtr<ULevelStreaming>> TransactionalStreams;

void OnTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
    if (!IsInGameThread() || Event.GetEventType() != ETransactionObjectEventType::UndoRedo ||
        !Event.GetChangedProperties().Contains(FName(TEXT("WorldAsset"))))
        return;
    ULevelStreaming* Stream = Cast<ULevelStreaming>(Object);
    if (!Stream || !TransactionalStreams.Contains(TWeakObjectPtr<ULevelStreaming>(Stream)))
        return;
    // UE4.26 restores WorldAsset in Undo but leaves its nonserialized package
    // caches intact. Its normal property callback invalidates both caches.
    FProperty* Property = FindFProperty<FProperty>(ULevelStreaming::StaticClass(), TEXT("WorldAsset"));
    if (!Property)
        return;
    FPropertyChangedEvent Changed(Property, EPropertyChangeType::ValueSet);
    Stream->PostEditChangeProperty(Changed);
}

bool ProjectPackage(const FString& Name)
{
    return P4IsValidContentPackage(Name);
}

bool CanonicalReference(const ULevelStreaming* Stream)
{
    const FString Package = Stream->GetWorldAssetPackageName();
    const FSoftObjectPath Path = Stream->GetWorldAsset().ToSoftObjectPath();
    return ProjectPackage(Package) && Path.GetSubPathString().IsEmpty() &&
        Path.GetAssetPathString() == P4ObjectPathOf(Package);
}

bool UniqueJsonKeys(const FString& Json)
{
    // The engine object deserializer silently replaces duplicate fields.
    // Refuse those before decoding a plan or opening an editor transaction.
    if (Json.Contains(TEXT("\\u0000"), ESearchCase::CaseSensitive))
        return false;
    for (TCHAR Character : Json)
        if (!Character)
            return false;
    auto Reader = TJsonReaderFactory<>::Create(Json.TrimStartAndEnd());
    TArray<TSet<FString>> Keys;
    TArray<bool> Objects;
    EJsonNotation Notation;
    bool Root = false;
    while (Reader->ReadNext(Notation))
    {
        if (Notation == EJsonNotation::Error)
            return false;
        const bool End = Notation == EJsonNotation::ObjectEnd || Notation == EJsonNotation::ArrayEnd;
        if (!End && Objects.Num() && Objects.Last())
        {
            const FString Key = Reader->GetIdentifier();
            if (Keys.Last().Contains(Key))
                return false;
            Keys.Last().Add(Key);
        }
        if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
        {
            if (!Root)
            {
                if (Notation != EJsonNotation::ObjectStart)
                    return false;
                Root = true;
            }
            Objects.Add(Notation == EJsonNotation::ObjectStart);
            Keys.Emplace();
        }
        else if (End)
        {
            if (!Objects.Num())
                return false;
            Objects.Pop();
            Keys.Pop();
        }
    }
    return Root && !Objects.Num() && Reader->GetErrorMessage().IsEmpty();
}

struct FRemap
{
    ULevelStreaming* Stream = nullptr;
    FString Target;
    FName LoadPackage;
    bool Loaded = false;
    bool Visible = false;
    bool EditorVisible = false;
};

bool Flags(const TSharedPtr<FJsonObject>& Object, const FString& Target, bool ForBake, FRemap& Row)
{
    if (!Object.IsValid() || !Object->HasTypedField<EJson::Boolean>(TEXT("loaded")) ||
        !Object->HasTypedField<EJson::Boolean>(TEXT("visible")) ||
        !Object->HasTypedField<EJson::Boolean>(TEXT("editor_visible")) ||
        !Object->TryGetBoolField(TEXT("loaded"), Row.Loaded) ||
        !Object->TryGetBoolField(TEXT("visible"), Row.Visible) ||
        !Object->TryGetBoolField(TEXT("editor_visible"), Row.EditorVisible))
        return false;
    FString Override;
    const bool HasOverride = Object->HasField(TEXT("package_to_load"));
    if (HasOverride &&
        (!Object->HasTypedField<EJson::String>(TEXT("package_to_load")) ||
            !Object->TryGetStringField(TEXT("package_to_load"), Override) ||
            (!Override.IsEmpty() && !ProjectPackage(Override))))
        return false;
    Row.Target = Target;
    Row.LoadPackage = FName(*(ForBake || !HasOverride ? Target : Override));
    Row.Loaded = ForBake || Row.Loaded;
    Row.Visible = ForBake || Row.Visible;
    Row.EditorVisible = ForBake || Row.EditorVisible;
    return true;
}

struct FMapping
{
    FRemap Default;
    bool PerInstance = false;
    TMap<FString, FRemap> Instances;
    TSet<FString> UsedInstances;
};

struct FSourcePose
{
    USceneComponent* Root = nullptr;
    FVector Location;
    FRotator Rotation;
    FVector Scale;
};
} //namespace P4StreamingInternals

void P4RegisterStreamingUndoCallbacks()
{
    if (IsInGameThread() && !P4StreamingInternals::UndoDelegate.IsValid())
        P4StreamingInternals::UndoDelegate = FCoreUObjectDelegates::OnObjectTransacted.AddStatic(
            &P4StreamingInternals::OnTransacted);
}

void P4UnregisterStreamingUndoCallbacks()
{
    FCoreUObjectDelegates::OnObjectTransacted.Remove(P4StreamingInternals::UndoDelegate);
    P4StreamingInternals::UndoDelegate.Reset();
    P4StreamingInternals::TransactionalStreams.Empty();
}

FString UP4AuthoringTools::StreamingState(UWorld* World)
{
    if (!IsInGameThread() || !World)
        return FString();
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (ULevelStreaming* Stream : World->GetStreamingLevels())
    {
        if (!Stream)
            continue;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("stream_name"), Stream->GetName());
        Row->SetStringField(TEXT("package"), Stream->GetWorldAssetPackageName());
        Row->SetStringField(TEXT("loaded_package"),
            Stream->GetLoadedLevel() ? Stream->GetLoadedLevel()->GetOutermost()->GetName() : FString());
        Row->SetStringField(TEXT("package_to_load"),
            Stream->PackageNameToLoad.IsNone() ? FString() : Stream->PackageNameToLoad.ToString());
        Row->SetBoolField(TEXT("loaded"), Stream->ShouldBeLoaded());
        Row->SetBoolField(TEXT("visible"), Stream->GetShouldBeVisibleFlag());
        Row->SetBoolField(TEXT("editor_visible"), Stream->GetShouldBeVisibleInEditor());
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    FString Json;
    auto Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Rows, Writer);
    return Json;
}

bool UP4AuthoringTools::RemapStreaming(UWorld* World, const FString& MappingJson, bool ForBake, bool MarkDirty)
{
    if (!IsInGameThread())
        return false;
    using namespace P4StreamingInternals;
    TSharedPtr<FJsonObject> Mapping;
    auto Reader = TJsonReaderFactory<>::Create(MappingJson);
    if (!World || !UniqueJsonKeys(MappingJson) || !FJsonSerializer::Deserialize(Reader, Mapping) || !Mapping.IsValid())
        return false;
    TMap<FString, FMapping> Rows;
    TSet<FName> Identities;
    // Check every supplied row, including stale rows, before changing anything.
    // A stale deleted row cannot recreate an entry in the world's actual list.
    for (const auto& Item : Mapping->Values)
    {
        FMapping Row;
        if (!ProjectPackage(Item.Key) || Identities.Contains(FName(*Item.Key)) || !Item.Value.IsValid() ||
            Item.Value->Type != EJson::Object)
            return false;
        const auto Object = Item.Value->AsObject();
        FString Target;
        if (!Object.IsValid() || !Object->HasTypedField<EJson::String>(TEXT("target")) ||
            !Object->TryGetStringField(TEXT("target"), Target) || !ProjectPackage(Target))
            return false;
        Row.PerInstance = Object->HasField(TEXT("instances"));
        if (!Row.PerInstance && !Flags(Object, Target, ForBake, Row.Default))
            return false;
        if (Row.PerInstance)
        {
            const TSharedPtr<FJsonObject>* Instances;
            if (!Object->TryGetObjectField(TEXT("instances"), Instances) || !Instances->IsValid())
                return false;
            TSet<FName> Names;
            for (const auto& Instance : (*Instances)->Values)
            {
                FRemap State;
                if (Instance.Key.IsEmpty() || FName(*Instance.Key).IsNone() || Names.Contains(FName(*Instance.Key)) ||
                    !Instance.Value.IsValid() || Instance.Value->Type != EJson::Object)
                    return false;
                const auto Data = Instance.Value->AsObject();
                if (!Flags(Data, Target, ForBake, State))
                    return false;
                FString InstanceTarget;
                if (Data->HasField(TEXT("target")) &&
                    (!Data->HasTypedField<EJson::String>(TEXT("target")) ||
                        !Data->TryGetStringField(TEXT("target"), InstanceTarget) || InstanceTarget != Target))
                    return false;
                Names.Add(FName(*Instance.Key));
                Row.Instances.Add(Instance.Key, State);
            }
        }
        Identities.Add(FName(*Item.Key));
        Rows.Add(Item.Key, Row);
    }
    TArray<FRemap> Plan;
    TSet<ULevelStreaming*> Seen;
    for (ULevelStreaming* Stream : World->GetStreamingLevels())
    {
        if (!Stream || Stream->GetTypedOuter<UWorld>() != World || Seen.Contains(Stream) ||
            !CanonicalReference(Stream) || Stream->GetIsRequestingUnloadAndRemoval() ||
            Stream->HasLoadRequestPending() ||
            Stream->GetCurrentState() == ULevelStreaming::ECurrentState::MakingVisible ||
            Stream->GetCurrentState() == ULevelStreaming::ECurrentState::MakingInvisible)
            return false;
        Seen.Add(Stream);
        FMapping* Row = Rows.Find(Stream->GetWorldAssetPackageName());
        if (!Row)
            return false;
        const FRemap* State = Row->PerInstance ? Row->Instances.Find(Stream->GetName()) : &Row->Default;
        if (!State)
            return false;
        if (Row->PerInstance)
            Row->UsedInstances.Add(Stream->GetName());
        FRemap Change = *State;
        Change.Stream = Stream;
        Plan.Add(Change);
    }
    for (const auto& Row : Rows)
        if (Row.Value.PerInstance && Row.Value.UsedInstances.Num() != Row.Value.Instances.Num())
            return false;
    if (!Plan.Num())
        return true;
    if (MarkDirty)
        for (auto It = TransactionalStreams.CreateIterator(); It; ++It)
            if (!It->IsValid())
                It.RemoveCurrent();
    const FScopedTransaction Transaction(NSLOCTEXT("P4MapKit", "RemapStreaming", "Remap streaming levels"), MarkDirty);
    if (MarkDirty)
        World->Modify();
    for (const FRemap& Row : Plan)
    {
        ULevelStreaming* Stream = Row.Stream;
        if (MarkDirty)
        {
            TransactionalStreams.Add(TWeakObjectPtr<ULevelStreaming>(Stream));
            Stream->SetFlags(RF_Transactional);
            Stream->Modify();
        }
        if (Stream->GetWorldAssetPackageFName() != FName(*Row.Target))
        {
            // A second remap of an already cleared stream must not call the
            // engine's SetLoadedLevel again while its old unload is pending.
            if (Stream->GetLoadedLevel())
                Stream->ClearLoadedLevel();
            Stream->SetWorldAssetByPackageName(FName(*Row.Target));
        }
        Stream->PackageNameToLoad = Row.LoadPackage;
        Stream->SetShouldBeLoaded(Row.Loaded);
        Stream->SetShouldBeVisible(Row.Visible);
        Stream->SetShouldBeVisibleInEditor(Row.EditorVisible);
        if (!ForBake && Stream->GetLoadedLevel())
            EditorLevelUtils::SetLevelVisibilityTemporarily(Stream->GetLoadedLevel(), Row.EditorVisible);
    }
    if (MarkDirty && Plan.Num())
        World->MarkPackageDirty();
    return true;
}

bool UP4AuthoringTools::LoadExportLevels(UWorld* World)
{
    if (!IsInGameThread() || !World)
        return false;
    using namespace P4StreamingInternals;
    TArray<FRemap> Before;
    TSet<ULevelStreaming*> Seen;
    const TArray<ULevelStreaming*> Streams = World->GetStreamingLevels();
    // Missing/deleted or mismatched worlds must fail before forcing any flags.
    for (ULevelStreaming* Stream : Streams)
    {
        if (!Stream || Stream->GetTypedOuter<UWorld>() != World || Seen.Contains(Stream) ||
            !CanonicalReference(Stream) || Stream->GetIsRequestingUnloadAndRemoval())
            return false;
        Seen.Add(Stream);
        const FString Package = Stream->GetWorldAssetPackageName();
        if (!ProjectPackage(Package) ||
            (!Stream->PackageNameToLoad.IsNone() && Stream->PackageNameToLoad != FName(*Package)) ||
            !FPackageName::DoesPackageExist(Package))
            return false;
        UWorld* Asset = Stream->GetWorldAsset().LoadSynchronous();
        if (!Asset || !Asset->PersistentLevel || Asset->GetStreamingLevels().Num() != 0 ||
            Asset->GetOutermost()->GetFName() != FName(*Package) ||
            (Stream->GetLoadedLevel() && Stream->GetLoadedLevel()->GetOutermost()->GetFName() != FName(*Package)))
            return false;
        FRemap State;
        State.Stream = Stream;
        State.Loaded = Stream->ShouldBeLoaded();
        State.Visible = Stream->GetShouldBeVisibleFlag();
        State.EditorVisible = Stream->GetShouldBeVisibleInEditor();
        Before.Add(State);
    }
    if (World->GetStreamingLevels() != Streams)
        return false;
    const auto Restore = [&Before]()
    {
        for (const FRemap& Row : Before)
        {
            Row.Stream->SetShouldBeLoaded(Row.Loaded);
            Row.Stream->SetShouldBeVisible(Row.Visible);
            Row.Stream->SetShouldBeVisibleInEditor(Row.EditorVisible);
            if (Row.Stream->GetLoadedLevel())
                EditorLevelUtils::SetLevelVisibilityTemporarily(Row.Stream->GetLoadedLevel(), Row.EditorVisible);
        }
    };
    for (ULevelStreaming* Stream : Streams)
    {
        if (!Stream)
            continue;
        Stream->SetShouldBeLoaded(true);
        Stream->SetShouldBeVisible(true);
        Stream->SetShouldBeVisibleInEditor(true);
    }
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    if (World->GetStreamingLevels() != Streams)
    {
        Restore();
        return false;
    }
    for (ULevelStreaming* Stream : Streams)
    {
        if (!Stream)
            continue;
        ULevel* Level = Stream->GetLoadedLevel();
        if (!Level || Level->GetOutermost()->GetFName() != Stream->GetWorldAssetPackageFName())
        {
            Restore();
            return false;
        }
        if (!World->GetLevels().Contains(Level))
        {
            Level->OwningWorld = World;
            Level->bIsVisible = false;
            World->AddToWorld(Level, Stream->LevelTransform, false);
        }
        // Visibility for collection is temporary and does not dirty authoring packages.
        EditorLevelUtils::SetLevelVisibilityTemporarily(Level, true);
    }
    return true;
}

bool UP4AuthoringTools::SetExportLevel(UWorld* World, const FString& Package)
{
    if (!IsInGameThread() || !World || !P4StreamingInternals::ProjectPackage(Package))
        return false;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->GetOutermost()->GetName() == Package)
        {
            World->SetCurrentLevel(Level);
            return true;
        }
    return false;
}

bool UP4AuthoringTools::SaveExportWorld(UWorld* World, const FString& Package)
{
    if (!IsInGameThread() || !World || !World->PersistentLevel || !P4StreamingInternals::ProjectPackage(Package) ||
        World->GetOutermost()->GetFName() == FName(*Package) ||
        !FPackageName::DoesPackageExist(World->GetOutermost()->GetName()))
        return false;
    ULevelStreaming* Stream = FLevelUtils::FindStreamingLevel(World->PersistentLevel);
    UPackage* SourcePackage = World->GetOutermost();
    const bool Dirty = SourcePackage->IsDirty();
    TArray<P4StreamingInternals::FSourcePose> Roots;
    if (Stream)
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (Actor && Actor->GetRootComponent())
            {
                P4StreamingInternals::FSourcePose Pose;
                Pose.Root = Actor->GetRootComponent();
                Pose.Location = Pose.Root->GetRelativeLocation();
                Pose.Rotation = Pose.Root->GetRelativeRotation();
                Pose.Scale = Pose.Root->GetRelativeScale3D();
                Roots.Add(Pose);
            }
    // Loaded sublevel actors already have the editor streaming transform. Save
    // their local coordinates, or the copy receives that transform a second time.
    if (Stream)
        FLevelUtils::RemoveEditorTransform(Stream, false);
    const bool Saved = UEditorLoadingAndSavingUtils::SaveMap(World, Package);
    // Restoring saved component transforms avoids inverse/apply floating point
    // drift, especially for rotated levels with nonuniform scale.
    for (const auto& Pose : Roots)
    {
        // Ordinary setters pass through a quaternion and lose authored rotator
        // winding. Restore the literal fields, then refresh derived transforms.
        Pose.Root->SetRelativeLocation_Direct(Pose.Location);
        Pose.Root->SetRelativeRotation_Direct(Pose.Rotation);
        Pose.Root->SetRelativeScale3D_Direct(Pose.Scale);
        Pose.Root->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::TeleportPhysics);
    }
    SourcePackage->SetDirtyFlag(Dirty);
    return Saved;
}

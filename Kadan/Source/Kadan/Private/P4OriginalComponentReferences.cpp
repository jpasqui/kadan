#include "P4OriginalComponentReferences.h"
#include "P4OriginalRecord.h"
#include "P4LevelContent.h"
#include "P4OriginalEngineFields.h"
#include "P4OriginalWeakComponentReferences.h"
#include "P4OriginalSplineCurves.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "UObject/UnrealType.h"

namespace
{
FDelegateHandle RecordedHandle;
void Remap(const UEditorEngine::ReplacementObjectMap& Replacements, UWorld* Scope, bool RecordedOnly)
{
    // A construction script replaces every component of its actor in one notification: each level's record and
    // game actors are read once for all of them. Unset is a level that is not the game's.
    TMap<UWorld*, TOptional<TSet<FName>>> Levels;
    for (const auto& Pair : Replacements)
    {
        UActorComponent* Old = Cast<UActorComponent>(Pair.Key);
        UActorComponent* New = Cast<UActorComponent>(Pair.Value);
        if (!Old || !IsValid(New) || Old->GetClass() != New->GetClass())
            continue;
        // A replacement notification is the identity proof. Never infer a replacement by name,
        // instantiate a component or transplant an alias to a different actor/world.
        AActor* Actor = Old->GetTypedOuter<AActor>();
        // Streaming actors report the persistent runtime world from GetWorld(). Their
        // original record and source actor identities belong to their own level's outer world.
        UWorld* SourceWorld = Actor && Actor->GetLevel() ? Cast<UWorld>(Actor->GetLevel()->GetOuter()) : nullptr;
        if (!IsValid(Actor) || Actor->IsTemplate() || New->GetTypedOuter<AActor>() != Actor || !Actor->GetWorld() ||
            !SourceWorld || (Scope && Actor->GetWorld() != Scope && SourceWorld != Scope))
            continue;
        TOptional<TSet<FName>>* Level = Levels.Find(SourceWorld);
        if (!Level)
        {
            FP4OriginalRecord Record;
            FString Error;
            Level = &Levels.Add(SourceWorld);
            if (P4ReadOriginalRecord(SourceWorld, Record, Error) && Record.Kind == TEXT("level"))
                *Level = P4ImportedLevelActors(SourceWorld);
        }
        if (RecordedOnly && !Level->IsSet())
            continue;
        // Creator-added actors have their own construction behavior. Only the actual
        // original actor family needs this compatibility repair after publication.
        if (Level->IsSet() && !Level->GetValue().Contains(Actor->GetFName()))
            continue;
        P4RestoreOriginalEngineFields(Old, New);
        P4RestoreOriginalSplineCurves(Old, New);
        for (TFieldIterator<FObjectProperty> Field(Actor->GetClass()); Field; ++Field)
        {
            const UClass* Owner = Cast<UClass>(Field->GetOwnerStruct());
            if (!Owner || !Owner->HasAnyClassFlags(CLASS_Native) ||
                Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly | CPF_SkipSerialization) ||
                !Field->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
                continue;
            for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
                if (Field->GetObjectPropertyValue_InContainer(Actor, Index) == Old)
                    Field->SetObjectPropertyValue_InContainer(Actor, New, Index);
        }
        P4RemapOriginalWeakComponentReferences(Replacements, Old, New, Actor);
    }
}
} //namespace

FP4OriginalComponentReferenceScope::FP4OriginalComponentReferenceScope(UWorld* World)
{
    if (GEditor && World)
        Handle = GEditor->OnObjectsReplaced().AddLambda(
            [World](const UEditorEngine::ReplacementObjectMap& Map) { Remap(Map, World, false); });
}
FP4OriginalComponentReferenceScope::~FP4OriginalComponentReferenceScope()
{
    if (GEditor && Handle.IsValid())
        GEditor->OnObjectsReplaced().Remove(Handle);
}
void P4StartOriginalComponentReferences()
{
    if (GEditor && !RecordedHandle.IsValid())
        RecordedHandle = GEditor->OnObjectsReplaced().AddLambda(
            [](const UEditorEngine::ReplacementObjectMap& Map) { Remap(Map, nullptr, true); });
}
void P4StopOriginalComponentReferences()
{
    if (GEditor && RecordedHandle.IsValid())
        GEditor->OnObjectsReplaced().Remove(RecordedHandle);
    RecordedHandle.Reset();
}

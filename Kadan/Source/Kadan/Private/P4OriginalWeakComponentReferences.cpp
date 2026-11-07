#include "P4OriginalWeakComponentReferences.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
struct FWeakAlias
{
    UObject* Container;
    FWeakObjectProperty* Property;
    int32 Index;
    USkinnedMeshComponent* PoseSlave;
};
UObject* PendingIdentity(const FWeakAlias& Alias)
{
    // Ordinary GetObjectPropertyValue/Get() discards pending-kill identity.
    return Alias.Property->GetPropertyValue_InContainer(Alias.Container, Alias.Index).Get(true);
}
} //namespace

int32 P4RemapOriginalWeakComponentReferences(
    const TMap<UObject*, UObject*>& Replacements, UActorComponent* Old, UActorComponent* New, AActor* Actor)
{
    UObject* const* Replacement = Replacements.Find(Old);
    if (!IsInGameThread() || !Old || !IsValid(New) || !IsValid(Actor) || Actor->IsTemplate() || !Actor->GetWorld() ||
        !Replacement || *Replacement != New || Old == New || Old->GetClass() != New->GetClass() ||
        Old->GetTypedOuter<AActor>() != Actor || New->GetTypedOuter<AActor>() != Actor || New->IsTemplate() ||
        !Actor->GetComponents().Contains(New))
        return 0;

    FWeakObjectProperty* PoseProperty = FindFProperty<FWeakObjectProperty>(
        USkinnedMeshComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USkinnedMeshComponent, MasterPoseComponent));
    TArray<FWeakAlias> Aliases;
    const auto Gather = [&](UObject* Source, UObject* Container, bool ReplacedContainer)
    {
        for (TFieldIterator<FWeakObjectProperty> Field(Container->GetClass()); Field; ++Field)
        {
            const UClass* Owner = Cast<UClass>(Field->GetOwnerStruct());
            if (!Owner || !Owner->HasAnyClassFlags(CLASS_Native) || !Field->PropertyClass ||
                !Field->PropertyClass->IsChildOf(UActorComponent::StaticClass()) ||
                Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly | CPF_SkipSerialization))
                continue;
            for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
            {
                FWeakAlias Alias{Container, *Field, Index,
                    *Field == PoseProperty ? Cast<USkinnedMeshComponent>(Container) : nullptr};
                if (Alias.PoseSlave == New)
                    continue;
                if (Field->GetPropertyValue_InContainer(Source, Index).Get(true) != Old)
                    continue;
                UObject* Current = PendingIdentity(Alias);
                // The stock component-instance cache excludes MasterPoseComponent
                // (BlueprintReadOnly, without Edit/Interp). A recreated slave can
                // therefore be nil despite its exact serialized old relationship.
                // Nil recovery is restricted to that native field and a real
                // container replacement; a different construction target wins.
                if (Current != Old && !(ReplacedContainer && Alias.PoseSlave && !Current))
                    continue;
                if (!Aliases.ContainsByPredicate(
                        [&](const FWeakAlias& Existing)
                        {
                            return Existing.Container == Container && Existing.Property == *Field &&
                                Existing.Index == Index;
                        }))
                    Aliases.Add(Alias);
            }
        }
    };
    Gather(Actor, Actor, false);
    for (UActorComponent* Component : Actor->GetComponents())
        if (IsValid(Component) && !Component->IsTemplate() && Component->GetTypedOuter<AActor>() == Actor)
            Gather(Component, Component, false);
    // Snapshot both endpoints before any old-master BeginDestroy. The old slave
    // is pending-kill, so its weak master still exposes Get(true) identity while
    // BeginDestroy's live-slave iteration skips it. Map order cannot substitute
    // component names or erase this authoritative serialized relationship.
    for (const TPair<UObject*, UObject*>& Pair : Replacements)
    {
        UActorComponent* Source = Cast<UActorComponent>(Pair.Key);
        UActorComponent* Container = Cast<UActorComponent>(Pair.Value);
        if (!Source || !IsValid(Container) || Source == Container || Source->GetClass() != Container->GetClass() ||
            Source->IsTemplate() || Container->IsTemplate() || Source->GetTypedOuter<AActor>() != Actor ||
            Container->GetTypedOuter<AActor>() != Actor || !Source->IsPendingKill() || Source->IsRegistered() ||
            Source->HasAnyFlags(RF_FinishDestroyed) || Actor->GetComponents().Contains(Source) ||
            !Actor->GetComponents().Contains(Container))
            continue;
        Gather(Source, Container, true);
    }

    USkinnedMeshComponent* OldMaster = Cast<USkinnedMeshComponent>(Old);
    USkinnedMeshComponent* NewMaster = Cast<USkinnedMeshComponent>(New);
    bool PoseLinks = Aliases.ContainsByPredicate([](const FWeakAlias& Alias) { return Alias.PoseSlave != nullptr; });
    bool Retired = false;
    if (PoseLinks)
    {
        // SetMasterPoseComponent sees pending-kill OldMaster as null and cannot
        // remove its reverse slave entries. Normal BeginDestroy would later clear
        // those live slaves, including a newly repaired link. Retire this already
        // destroyed, authoritatively replaced component before setting the new link.
        bool Safe = OldMaster && NewMaster && OldMaster->IsPendingKill() && !OldMaster->IsRegistered() &&
            !Actor->GetComponents().Contains(OldMaster) && !OldMaster->HasAnyFlags(RF_FinishDestroyed);
        if (Safe && !OldMaster->HasAnyFlags(RF_BeginDestroyed))
            for (const TWeakObjectPtr<USkinnedMeshComponent>& Weak : OldMaster->GetSlavePoseComponents())
                if (USkinnedMeshComponent* Slave = Weak.Get())
                    if (Slave->GetTypedOuter<AActor>() != Actor || Slave->MasterPoseComponent.Get(true) != OldMaster)
                    {
                        Safe = false;
                        break;
                    }
        if (Safe)
        {
            if (!OldMaster->HasAnyFlags(RF_BeginDestroyed))
                OldMaster->ConditionalBeginDestroy();
            Retired = OldMaster->HasAnyFlags(RF_BeginDestroyed);
        }
    }
    int32 Count = 0;
    for (const FWeakAlias& Alias : Aliases)
    {
        if (!IsValid(Alias.Container))
            continue;
        UObject* Current = PendingIdentity(Alias);
        if (Alias.PoseSlave)
        {
            // Only the captured exact old identity may survive its own normal
            // disconnection. A valid different/new construction target wins.
            if (!Retired || Alias.PoseSlave == NewMaster || (Current && Current != Old))
                continue;
            Alias.PoseSlave->SetMasterPoseComponent(NewMaster, true);
            if (Alias.PoseSlave->MasterPoseComponent.Get() == NewMaster)
                ++Count;
        }
        else if (Current == Old)
        {
            Alias.Property->SetObjectPropertyValue_InContainer(Alias.Container, New, Alias.Index);
            ++Count;
        }
    }
    return Count;
}

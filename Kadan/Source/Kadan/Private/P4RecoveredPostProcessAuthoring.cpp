#include "P4RecoveredPostProcessAuthoring.h"
#include "P4RecoveredPostProcessStorage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

bool P4RefuseRecoveredPostProcessAuthoring(
    const UObject* Owner, const FProperty* Property, const void* Data, FString& Reason)
{
    Reason.Reset();
    const UClass* Declaring = FindObject<UClass>(nullptr, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
    if (!Declaring || !Property)
        return false;
    // Descendant refusal is deliberately broader than import compatibility: an
    // inherited host-only property must never become a raw game path. This
    // grants no source class/storage admission to that descendant.
    const bool InheritedOwner = Owner && Owner->IsA(Declaring);
    const FStructProperty* Struct = CastField<FStructProperty>(Property);
    const FStructProperty* DeclaredFork = FindFProperty<FStructProperty>(Declaring, TEXT("ForkSettings"));
    const UStruct* DeclaredRegion = DeclaredFork && DeclaredFork->GetOwnerStruct() == Declaring ? DeclaredFork->Struct
                                                                                                : nullptr;
    const UScriptStruct* KnownStruct = FindObject<UScriptStruct>(
        nullptr, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessForkSettings"));
    const bool RegionMetadata = Property->GetOwnerStruct() == Declaring ||
        (DeclaredRegion &&
            (Property->GetOwnerStruct() == DeclaredRegion || (Struct && Struct->Struct == DeclaredRegion))) ||
        (KnownStruct && (Property->GetOwnerStruct() == KnownStruct || (Struct && Struct->Struct == KnownStruct)));
    if (!InheritedOwner && !RegionMetadata)
        return false;
    FP4RecoveredPostProcessStorage Storage;
    FString Invalid;
    if (!P4GetRecoveredPostProcessStorage(Declaring, Storage, Invalid))
    {
        if (!Invalid.IsEmpty() && (InheritedOwner || RegionMetadata))
        {
            Reason = Invalid;
            return true;
        }
        return false;
    }
    bool Host = Property == Storage.ForkSettings || Property->GetOwnerStruct() == Storage.ForkSettings->Struct ||
        (Struct && Struct->Struct == Storage.ForkSettings->Struct);
    if (!Host && InheritedOwner && Data)
    {
        const UPTRINT Begin = reinterpret_cast<UPTRINT>(Storage.ForkSettings->ContainerPtrToValuePtr<void>(Owner));
        const UPTRINT At = reinterpret_cast<UPTRINT>(Data);
        Host = At >= Begin && At - Begin < UPTRINT(Storage.ForkSettings->GetSize());
    }
    if (!Host)
        return false;
    Reason = TEXT(
        "these post-process values are preserved for import, but changes do not yet have a game export route.");
    return true;
}

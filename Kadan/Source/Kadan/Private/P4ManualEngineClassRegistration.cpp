#include "P4ManualEngineClassRegistration.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/LinkerLoad.h"
#include "UObject/UObjectGlobals.h"

FP4ManualEngineClassRegistration::FP4ManualEngineClassRegistration(UClass* InClass, UObject* (*Register)())
    : Class(InClass)
{
    if (!IsInGameThread() || !::IsValid(Class) || Class->GetDefaultObject(false))
        return;
    UPackage* Package = Class->GetOutermost();
    if (!Package || Class->GetOuter() != Package || !Package->GetName().StartsWith(TEXT("/Script/")))
        return;
    PackageName = Package->GetName();
    ClassName = Class->GetName();
    Valid = true;
    // Match CreateDefaultObject's exact notification predicate. Never modify
    // package flags or the initial-load/EDL globals to avoid its native lifecycle.
    Notified = GIsInitialLoad && Package->HasAnyPackageFlags(PKG_CompiledIn) &&
        !Package->HasAnyPackageFlags(PKG_RuntimeGenerated);
    if (!Notified)
        return;
    NotifyRegistrationEvent(*PackageName, *ClassName, ENotifyRegistrationType::NRT_Class,
        ENotifyRegistrationPhase::NRP_Added, Register, true);
    const FString CDOName = Class->GetDefaultObjectName().ToString();
    NotifyRegistrationEvent(*PackageName, *CDOName, ENotifyRegistrationType::NRT_ClassCDO,
        ENotifyRegistrationPhase::NRP_Added, Register, true);
    NotifyRegistrationEvent(
        *PackageName, *ClassName, ENotifyRegistrationType::NRT_Class, ENotifyRegistrationPhase::NRP_Started);
}

bool FP4ManualEngineClassRegistration::FinishClass()
{
    if (!Valid || !IsInGameThread() || !::IsValid(Class) || Class->GetOutermost()->GetName() != PackageName ||
        Class->GetName() != ClassName)
        return false;
    if (Finished)
        return true;
    if (!Class->ClassAddReferencedObjects)
        return false;
    // Classes made after the engine's startup-wide assembly sweep must be
    // ready for parallel GC before any CDO/instance becomes visible. Use the
    // stock assembler: it preserves reflected references and the native ARO.
    Class->AssembleReferenceTokenStream();
    if (!Class->HasAnyClassFlags(CLASS_TokenStreamAssembled))
        return false;
    if (Notified)
        NotifyRegistrationEvent(
            *PackageName, *ClassName, ENotifyRegistrationType::NRT_Class, ENotifyRegistrationPhase::NRP_Finished);
    Finished = true;
    return true;
}

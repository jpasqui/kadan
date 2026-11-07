#pragma once
#include "CoreMinimal.h"

class UWorld;
enum class EP4LevelClassDependenciesError : uint8
{ None, EditorThreadRequired, LevelRequired, ActorUnavailable, ClassCycle, LimitExceeded, PackageIdentityUnreadable };

struct FP4LevelActorClassDependency
{
    FString Actor, ActorClass, Class, Package;
    bool ScriptPackage = false;
};

struct KADAN_API FP4LevelClassDependenciesResult
{
    EP4LevelClassDependenciesError Error = EP4LevelClassDependenciesError::None;
    FString ErrorDetail, SourcePackage;
    TArray<FP4LevelActorClassDependency> Dependencies;
    TArray<FString> PackagedClasses;
    bool WholeLevel = false;
    bool IsValid() const { return Error == EP4LevelClassDependenciesError::None; }
};

// A dependency census, not source authority. Empty ActorNames inspects every
// live actor and child actor. Callers must qualify every packaged class source
// and the separately verified Blueprint behavior before production admission.
KADAN_API FP4LevelClassDependenciesResult P4InspectLevelClassDependencies(
    UWorld* World, const TArray<FString>& ActorNames = TArray<FString>());

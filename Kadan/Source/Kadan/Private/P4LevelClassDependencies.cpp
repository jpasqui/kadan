#include "P4LevelClassDependencies.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

FP4LevelClassDependenciesResult P4InspectLevelClassDependencies(UWorld* World, const TArray<FString>& ActorNames)
{
    FP4LevelClassDependenciesResult Result;
    const auto Fail = [&](EP4LevelClassDependenciesError Code, const FString& Why)
    {
        Result.Error = Code;
        Result.ErrorDetail = Why;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(EP4LevelClassDependenciesError::EditorThreadRequired,
            TEXT("level-class-dependencies-editor-thread-required"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Fail(EP4LevelClassDependenciesError::LevelRequired, TEXT("level-class-dependencies-level-required"));
    Result.SourcePackage = World->GetOutermost()->GetName();
    Result.WholeLevel = ActorNames.Num() == 0;
    TArray<AActor*> Actors;
    if (Result.WholeLevel)
    {
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (IsValid(Actor) && !Actor->IsPendingKill())
                Actors.AddUnique(Actor);
    }
    else
    {
        for (const FString& Name : ActorNames)
        {
            AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
            if (!IsValid(Actor) || Actor->IsPendingKill())
                return Fail(EP4LevelClassDependenciesError::ActorUnavailable,
                    TEXT("level-class-dependencies-actor-unavailable:") + Name);
            Actors.AddUnique(Actor);
        }
    }
    for (int32 Index = 0; Index < Actors.Num(); ++Index)
    {
        if (Actors.Num() > 65536)
            return Fail(EP4LevelClassDependenciesError::LimitExceeded, TEXT("level-class-dependencies-actor-limit"));
        AActor* Actor = Actors[Index];
        TArray<AActor*> Children;
        Actor->GetAllChildActors(Children, false);
        for (AActor* Child : Children)
            if (IsValid(Child) && !Child->IsPendingKill())
                Actors.AddUnique(Child);
        TSet<const UClass*> Seen;
        for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
        {
            if (Seen.Contains(Class))
                return Fail(EP4LevelClassDependenciesError::ClassCycle, TEXT("level-class-dependencies-class-cycle"));
            Seen.Add(Class);
            if (Seen.Num() > 128 || Result.Dependencies.Num() >= 262144)
                return Fail(
                    EP4LevelClassDependenciesError::LimitExceeded, TEXT("level-class-dependencies-class-limit"));
            const FString Package = Class->GetOutermost()->GetName();
            if (!FPackageName::IsValidLongPackageName(Package, true))
                return Fail(EP4LevelClassDependenciesError::PackageIdentityUnreadable,
                    TEXT("level-class-dependencies-package-unreadable:") + Package);
            FP4LevelActorClassDependency Dependency;
            Dependency.Actor = Actor->GetPathName();
            Dependency.ActorClass = Actor->GetClass()->GetPathName();
            Dependency.Class = Class->GetPathName();
            Dependency.Package = Package;
            Dependency.ScriptPackage = Package.StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive);
            if (!Dependency.ScriptPackage)
                Result.PackagedClasses.AddUnique(Package);
            Result.Dependencies.Add(MoveTemp(Dependency));
        }
    }
    Result.PackagedClasses.Sort(
        [](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    return Result;
}

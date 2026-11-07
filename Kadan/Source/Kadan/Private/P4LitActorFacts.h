#pragma once
#include "CoreMinimal.h"

class UWorld;

enum class EP4LitActorFactsError : uint8
{ None, EditorThreadRequired, LevelRequired, ActorsRequired, ActorUnsupported, CookFailed, NativeDataUnsupported, DigestFailed };
struct FP4LitActorFactsResult
{
    EP4LitActorFactsError Error = EP4LitActorFactsError::None;
    FString ErrorDetail, Package, Digest;
    TArray<FString> Actors;
    int32 Objects = 0, ContentLines = 0;
    bool IsValid() const { return Error == EP4LitActorFactsError::None && !Digest.IsEmpty(); }
};

// Fresh native cooked facts for selected stock static-mesh actors. The sole excluded
// native tail is the parser's typed derived lighting-LOD GUID state. Registry/cook
// completion, dependencies, source custody and creator admission remain separate.
KADAN_API FP4LitActorFactsResult P4InspectLitActorFactsNative(UWorld* World,
    const TArray<FString>& ActorNames);

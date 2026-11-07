#pragma once
#include "CoreMinimal.h"

class UWorld;
class ULevel;
class AActor;
class FLightingBuildOptions;
struct FP4AddedLightingState;

// The caller first uses ExportActors/SaveLit to classify and copy only added
// static mesh actors. This scope attaches that new world to the real editor
// scene; it neither creates lights nor copies original lighting registries.
class KADAN_API FP4AddedLightingAttachment
{
public:
    FP4AddedLightingAttachment();
    ~FP4AddedLightingAttachment();
    FP4AddedLightingAttachment(const FP4AddedLightingAttachment&) = delete;
    FP4AddedLightingAttachment& operator=(const FP4AddedLightingAttachment&) = delete;
    bool Attach(UWorld* Map, UWorld* Creator, const TArray<AActor*>& Added, FString& Error);
    bool Detach(FString& Error);
    bool BuildOptions(FLightingBuildOptions& Options, FString& Error) const;
private:
    TSharedPtr<FP4AddedLightingState> State;
};

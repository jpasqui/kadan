#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "P4KadanImportTools.generated.h"

/** Bounded imports through Kadan's existing native level backends. */
UCLASS()
class KADAN_API UP4KadanImportTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Import this level with the panel's fallback: actors that cannot be reconstructed stay game-owned.
     * Does not acquire the level's dependency closure or claim complete scene recovery. Returns the native import report. */
    UFUNCTION(BlueprintCallable, Category="Kadan|Import")
    static FString ImportLevelWithGameFallback(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk = true);
};

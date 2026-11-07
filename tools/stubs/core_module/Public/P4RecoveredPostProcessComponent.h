#pragma once
#include "CoreMinimal.h"
#include "Components/PostProcessComponent.h"
#include "P4RecoveredPostProcessSettings.h"
#include "P4RecoveredPostProcessComponent.generated.h"

// Source storage and stock component lifecycle; custom game rendering is separate.
UCLASS(ClassGroup=Rendering,editinlinenew,meta=(BlueprintSpawnableComponent))
class P4STUBSCORE_API UP4RecoveredPostProcessComponent : public UPostProcessComponent
{
    GENERATED_BODY()
public:
    UP4RecoveredPostProcessComponent(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    FP4RecoveredPostProcessForkSettings ForkSettings;
protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    // UHT must emit this child's structured-archive adapter. Inheriting the base
    // adapter would call its qualified classic serializer, bypassing this body.
    virtual void Serialize(FArchive& Ar) override;
};

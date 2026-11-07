// Native storage for observed game-engine light fields. Host offsets are owned by UHT.
// This preserves data; Nintendo shader behavior is not implemented here.
#pragma once
#include "CoreMinimal.h"
#include "Components/DirectionalLightComponent.h"
#include "P4RecoveredDirectionalLightComponent.generated.h"

class UTexture2D;

UCLASS(ClassGroup=Lights, editinlinenew)
class P4STUBSCORE_API UP4RecoveredDirectionalLightComponent : public UDirectionalLightComponent
{
	GENERATED_BODY()
public:
	UP4RecoveredDirectionalLightComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Game")
	bool bC4Param;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category="Game")
	FLinearColor C4Color;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param11;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param13;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Game")
	float C4Param17;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Game")
	FVector4 C4LensFlare0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Game")
	FVector4 C4LensFlare1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Game")
	UTexture2D* C4Texture0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Game")
	bool bC4ShadowBoundsOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Game")
	FVector C4ShadowBoundsOverride;

};

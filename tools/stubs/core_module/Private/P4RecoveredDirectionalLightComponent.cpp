#include "P4RecoveredDirectionalLightComponent.h"

UP4RecoveredDirectionalLightComponent::UP4RecoveredDirectionalLightComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bC4Param = false;
	C4Color = FLinearColor(0.0f,0.0f,0.0f,0.0f);
	C4Param0 = 1.0f;
	C4Param1 = 1.0f;
	C4Param2 = 0.0f;
	C4Param3 = 1.0f;
	C4Param4 = 0.0f;
	C4Param5 = 1.0f;
	C4Param6 = 1.0f;
	C4Param7 = 1.0f;
	C4Param8 = 0.0f;
	C4Param9 = -1.0f;
	C4Param10 = 1.0f;
	C4Param11 = -1.0f;
	C4Param12 = 10000.0f;
	C4Param13 = 0.0f;
	C4Param14 = 1000.0f;
	C4Param15 = 1000.0f;
	C4Param16 = -1.0f;
	C4Param17 = 0.0f;
	C4LensFlare0 = FVector4(0.0f,0.0f,0.0f,0.0f);
	C4LensFlare1 = FVector4(0.0f,0.0f,0.0f,0.0f);
	C4Texture0 = nullptr;
	bC4ShadowBoundsOverride = false;
	C4ShadowBoundsOverride = FVector(0.0f,0.0f,0.0f);
}

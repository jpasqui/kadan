#pragma once
#include "CoreMinimal.h"

// Editor stand-ins for game functions whose declared body (P4Stubs' "return {}") gives
// the game's animation Blueprints a state the game's objects are never in. Started with the module.
void P4StartGameFunctionStandIns();
void P4StopGameFunctionStandIns();
// The game functions that have a stand-in now, by path.
TArray<FString> P4GameFunctionStandIns();

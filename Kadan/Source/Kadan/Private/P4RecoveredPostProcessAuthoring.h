#pragma once
#include "CoreMinimal.h"
class FProperty;
// Native import storage does not itself qualify a game actor edit/export route.
// Data is optional, but lets direct nested native leaf calls retain region custody.
KADAN_API bool P4RefuseRecoveredPostProcessAuthoring(const UObject* Owner,const FProperty* Property,
    const void* Data,FString& Reason);

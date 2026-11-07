#pragma once
#include "CoreMinimal.h"

class UWorld;
class UPackage;

// Original-level reconstruction needs registered/rendered components, but it does not run physics
// or collision queries while decoded properties and attachments are still being restored. The
// physics scene is transient: a normal editor load creates one from the preserved collision data.
KADAN_API UWorld* P4CreateOriginalImportWorld(FName Name, UPackage* Package);

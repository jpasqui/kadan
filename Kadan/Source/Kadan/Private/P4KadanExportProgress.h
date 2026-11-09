#pragma once
#include "CoreMinimal.h"

// Runs synchronous export work with a repainting, indeterminate stage window.
KADAN_API void P4KadanProgressOperation(TFunction<void()> Work);

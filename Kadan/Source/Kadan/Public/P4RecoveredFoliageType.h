#pragma once
#include "CoreMinimal.h"
#include "FoliageType_InstancedStaticMesh.h"

// A separate reflected class owns the game's three extra properties. Its
// constructor and virtual behavior remain the actual native foliage parent's;
// no C++ subclass requires UE's nonexported MinimalAPI virtual definitions.
KADAN_API UClass* P4RecoveredFoliageClass();
KADAN_API void P4RegisterRecoveredFoliageType();
KADAN_API UFoliageType_InstancedStaticMesh* P4NewRecoveredFoliage(UObject* Outer = nullptr,
    FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags);

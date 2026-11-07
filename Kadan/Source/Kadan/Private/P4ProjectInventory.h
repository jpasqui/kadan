#pragma once
#include "CoreMinimal.h"

// Live editor discovery. This layer never reads kit settings or writes assets.
namespace P4ProjectInventory
{
    FString InventoryJson();
    FString WorldPlanJson(const FString& WorldPackage);
}

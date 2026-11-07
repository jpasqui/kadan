#pragma once

#include "CoreMinimal.h"

struct FP4KadanImportPlan;
struct FP4KadanImportResult;
class SWidget;
class SWindow;

TSharedRef<SWindow> P4KadanImportRepair(const TSharedRef<SWidget>& Parent,
    const FP4KadanImportPlan& Plan, const TSet<FString>& Selected, TFunction<void()> Reimport);
void P4KadanUpgradeGuide(const TSharedRef<SWidget>& Parent);
KADAN_API TArray<FString> P4KadanImportOutcomes(const FP4KadanImportResult& Result);

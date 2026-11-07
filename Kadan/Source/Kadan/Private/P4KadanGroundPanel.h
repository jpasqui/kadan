#pragma once

#include "CoreMinimal.h"
#include "P4GroundTypes.h"

class AActor;
class SWidget;

struct FP4KadanGroundResult
{
    int32 Applied = 0;
    int32 Skipped = 0;
    int32 Failed = 0;
    TArray<FString> Rows;
    FString Summary() const;
};

FP4KadanGroundResult P4KadanApplyGroundType(const TArray<AActor*>& Actors, const FP4GroundType& Type);
TSharedRef<SWidget> P4KadanGroundPanel();

#pragma once

#include "CoreMinimal.h"
#include "P4ModDescriptor.h"

class SWidget;
class SWindow;

struct FP4KadanDraftSaveResult
{
    bool Saved = false;
    FString Message;
    FString Detail;
};

TSharedRef<SWindow> P4KadanModDraftChoice(const TSharedRef<SWidget>& Parent, const FP4ModDescriptor& Draft,
    TFunction<FP4KadanDraftSaveResult()> Save, TFunction<void()> Continue);

#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/wwise_pcm.h"
class USoundWave;
struct FP4ExportModRequest;
struct FP4ExportModResult;
KADAN_API bool P4CreatorWaveSample(USoundWave* Wave, P4MapKit::WwisePCM::Sample& Out, FString& Error);
bool P4ExportCreatorSoundBank(const FP4ExportModRequest& Request, const TArray<USoundWave*>& Sounds,
    const FString& Metadata, FP4ExportModResult& Result, FString& Error);

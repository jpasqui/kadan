#pragma once
#include "CoreMinimal.h"
#include <vector>

// Probe exact recovered PP exports before the caller's full native-tail admission.
KADAN_API bool P4RecoveredPostProcessCookHasFork(const std::vector<uint8>& Header,
    const std::vector<uint8>& Body,bool& NeedsMerge,FString& Error);
// Source inputs supply original tag identity/order only. Actual cooked fields supply
// every value. RelocationSafe is required only for a serialized extension merge.
KADAN_API bool P4TranslateRecoveredPostProcessCook(std::vector<uint8>& Header,std::vector<uint8>& Body,
    const std::vector<uint8>* SourceHeader,const std::vector<uint8>* SourceBody,bool RelocationSafe,FString& Error);
// The record an imported level keeps of the game's post-process components: the identity and order of
// their tags and Settings members, never a value, bound to the source digest of the game's file. Json is
// empty when the game's file has no post-process component.
KADAN_API bool P4RecoveredPostProcessSourceOrder(const std::vector<uint8>& SourceHeader,const std::vector<uint8>& SourceBody,
    const FString& SourceDigest,FString& Json,FString& Error);
// The translation with that record in place of the game's file. A record of another source digest, or
// one that does not read, is refused by name.
KADAN_API bool P4TranslateRecoveredPostProcessCookWithOrder(std::vector<uint8>& Header,std::vector<uint8>& Body,
    const FString& Json,const FString& SourceDigest,bool RelocationSafe,FString& Error);

#pragma once
#include "CoreMinimal.h"

class FProperty;
class FStructProperty;

// A view of genuine native storage, never an invented reflected Settings member.
// Exact class/struct identity and every fork field are validated before admission.
struct FP4RecoveredPostProcessStorage
{
    const UClass* ActualClass=nullptr;
    const FStructProperty* StockSettings=nullptr;
    const FStructProperty* ForkSettings=nullptr;
};
struct FP4RecoveredPostProcessMember
{
    const FStructProperty* Container=nullptr;
    const FProperty* Property=nullptr;
    bool OriginalDepthOfFieldMethod=false;
};

// false + empty Error means unrelated class; false + Error means the exact known
// storage identity exists but its native contract is invalid. No IsChildOf alias.
P4BLUEPRINTIMPORT_API bool P4GetRecoveredPostProcessStorage(const UClass* Actual,
    FP4RecoveredPostProcessStorage& Out,FString& Error);
// Unknown original members remain unsupported. Ordinary common members resolve
// to inherited Settings; the complete known fork set resolves to ForkSettings.
P4BLUEPRINTIMPORT_API bool P4ResolveRecoveredPostProcessMember(const FP4RecoveredPostProcessStorage& View,
    FName OriginalMember,FP4RecoveredPostProcessMember& Out);
P4BLUEPRINTIMPORT_API void* P4RecoveredPostProcessMemberData(UObject* Object,
    const FP4RecoveredPostProcessStorage& View,const FP4RecoveredPostProcessMember& Member);
P4BLUEPRINTIMPORT_API const void* P4RecoveredPostProcessMemberData(const UObject* Object,
    const FP4RecoveredPostProcessStorage& View,const FP4RecoveredPostProcessMember& Member);

// Exact source enum meaning, separate from stock Bokeh/Gaussian/Circle choices.
// These helpers validate the actual enum property before translating names.
P4BLUEPRINTIMPORT_API bool P4RecoveredPostProcessSourceEnum(const FP4RecoveredPostProcessMember& Member,
    const FString& OriginalName,FString& NativeName);
P4BLUEPRINTIMPORT_API bool P4RecoveredPostProcessNativeEnum(const FP4RecoveredPostProcessMember& Member,
    const FString& NativeName,FString& OriginalName);

#pragma once
#include "CoreMinimal.h"

class FProperty;
class FP4DecodedPropertyWriter;
namespace P4MapKit { struct FDecodedValue; }

// Shared only by this exact Settings adapter and its strict coverage reader.
bool P4RecoveredPostProcessOriginalMemberIndex(const FString& Original, FString& Name, int32& Place);

// Writes original Settings paths into the exact typed storage child. Unrelated
// objects keep the ordinary writer. Errors and deferred references remain owned
// by the caller's existing writer; this does not grant an edited-export route.
enum class EP4RecoveredPostProcessWrite : uint8 { NotApplicable, Applied, Refused };
KADAN_API EP4RecoveredPostProcessWrite P4WriteRecoveredPostProcessSettings(
    FP4DecodedPropertyWriter& Writer, UObject* Target, const FProperty* Property, int32 Place,
    const P4MapKit::FDecodedValue& Value, const FString& Where);

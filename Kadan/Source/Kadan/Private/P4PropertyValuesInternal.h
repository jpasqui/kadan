// Internal to the property values: what the values file and the property text file share.
#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

namespace P4PropertyValuesDetail
{
struct FResolvedEditProperty
{
    FProperty* Property = nullptr;
    void* Value = nullptr;
    bool Single = false;
    FEditPropertyChain Chain;
    TMap<FString, int32> ArrayIndices;
};
bool HasEmbeddedNul(const FString& Text);
bool ResolveProperty(UObject* Object, FName Name, bool Editable, FResolvedEditProperty& Out);
bool JsonValue(FProperty* Property, const void* Value, FString& Out, bool Single = false);
} // namespace P4PropertyValuesDetail

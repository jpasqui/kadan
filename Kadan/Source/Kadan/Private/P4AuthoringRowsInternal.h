// Internal to the authoring rows: exact JSON admission, canonical values and readback, which the
// rows' files share (P4ExactJson.cpp, P4AuthoringCanonical.cpp, P4AuthoringReadback.cpp).
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;
class FMapProperty;
class UStruct;
class UDataTable;
class UObject;

namespace P4AuthoringRowsDetail
{
// P4ExactJson.cpp
bool P4HasNul(const FString& Text);
FString P4JsonString(const FString& Text);
bool P4ExactJson(const TSharedPtr<FJsonValue>& Value, FString& Out, int32 Depth = 0);
bool P4AdmissionFail(FString& Error, const FString& Label, const TCHAR* Reason);
// Token admission precedes the recursive DOM parser. Duplicate JSON keys
// must not disappear into FJsonObject's map before schema validation.
bool P4TableJsonTokens(const FString& Json, FString& Error);
bool ExactIntegerJson(FProperty* Property, TSharedPtr<FJsonValue>& Value, int32 Depth = 0, bool Single = false);

// P4AuthoringCanonical.cpp
FProperty* P4FieldProperty(UStruct* Struct, const FString& Key, int32& Slot);
bool P4CanonicalFields(UStruct* Struct, TSharedPtr<FJsonObject>& Object, const FString& Label, FString& Error,
    int32 Depth, bool Row = false, bool CheckReferences = true);
bool P4CanonicalValue(FProperty* Property, TSharedPtr<FJsonValue>& Value, const FString& Label, FString& Error,
    int32 Depth, bool Single = false, bool CheckReferences = true);
bool P4CanonicalRows(UDataTable* Table, const FString& Json, TArray<TSharedPtr<FJsonValue>>& Rows, FString& Error,
    bool CheckReferences = true);
bool P4CanonicalAsset(UObject* Asset, const FString& Json, TSharedPtr<FJsonObject>& Fields, FString& Canonical,
    FString& Error, bool CheckReferences);
void P4CollectAssetReferences(FProperty* Property, const TSharedPtr<FJsonValue>& Value,
    TArray<TSharedPtr<FJsonValue>>& References, bool Single = false);
bool P4WriteAssetValue(
    FProperty* Property, void* Destination, const TSharedPtr<FJsonValue>& Value, UObject* Owner, bool Single = false);

// P4AuthoringReadback.cpp
bool P4ScalarProjection(FProperty* Property, const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonValue>& Projected);
bool P4MapEntries(FMapProperty* Property, const TSharedPtr<FJsonValue>& Value,
    TMap<FString, TSharedPtr<FJsonValue>>& Entries, const FString& Label, FString& Error);
bool P4ReadbackValue(FProperty* Property, const TSharedPtr<FJsonValue>& Source, const TSharedPtr<FJsonValue>& Actual,
    TSharedPtr<FJsonValue>& SourceProjection, TSharedPtr<FJsonValue>& ActualProjection, const FString& Label,
    FString& Error, int32 Depth = 0, bool Single = false);

} // namespace P4AuthoringRowsDetail

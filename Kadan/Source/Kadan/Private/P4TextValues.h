#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

// Transport contains runtime localization identity, never a display-string
// approximation. Unsupported histories/flags refuse before table publication.
bool P4TextJsonToString(const TSharedPtr<FJsonValue>& Value, FString& Out);
KADAN_API bool P4TextRuntimeJson(const FText& Value, FString& Out);

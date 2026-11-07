#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include <cstdint>
#include <vector>

// The largest file Kadan reads whole, such as a package header or body.
constexpr int64 P4MaxFileBytes = 64 * 1024 * 1024;

// Whether A and B are the same text, letter case included.
bool P4SameText(const FString& A, const FString& B);

// Whether Text is a SHA-256 digest written as 64 lowercase hexadecimal digits.
bool P4IsSha256Hex(const FString& Text);

// Reads a file of at most P4MaxFileBytes.
bool P4LoadFileBytes(const FString& File, std::vector<std::uint8_t>& Out);

// Parses Text as a JSON object. Out is left as it was when Text is not one.
bool P4ParseJson(const FString& Text, TSharedPtr<FJsonObject>& Out);

// Parses Text as a JSON object, or gives null.
TSharedPtr<FJsonObject> P4ParseJsonObject(const FString& Text);

// JSON on one line, without spaces.
FString P4CondensedJson(const TSharedRef<FJsonObject>& Object);
FString P4CondensedJson(const TArray<TSharedPtr<FJsonValue>>& Values);
FString P4CondensedJson(const TSharedPtr<FJsonValue>& Value);

#pragma once
#include "CoreMinimal.h"

// Runtime declarations only. This boundary never reads a captured reflection file.
struct FP4NativeReflectionProperty
{
    FString Name, EnumPath;
    uint64 Flags = 0;
    int32 ArrayDim = 1;
    bool IsReturn = false, IsOut = false, IsReference = false;
};
struct FP4NativeReflectionFunction
{
    FString Path;
    uint32 Flags = 0;
    TArray<FP4NativeReflectionProperty> Parameters;
    TOptional<FP4NativeReflectionProperty> Return;
};
struct FP4NativeReflectionStructure
{
    FString Path, SuperPath;
    TArray<FP4NativeReflectionProperty> Fields;
};
struct FP4NativeReflectionEnum
{
    FString Path, CppType;
    uint8 CppForm = 255;
    uint32 EnumFlags = 0;
    TArray<FString> Values;
    TArray<int64> NumericValues;
};
struct FP4NativeBlueprintReflection
{
    TArray<FP4NativeReflectionFunction> Functions;
    TArray<FP4NativeReflectionStructure> Structures;
    TArray<FP4NativeReflectionEnum> Enums;
    TArray<FString> Refused;
};
// Modules are exact logical module names, without /Script/. Every requested module
// must already be loaded. RequiredPaths uses the planner's dot-separated function
// path grammar; absent declarations remain absent and produce an explicit refusal.
KADAN_API bool P4CaptureNativeBlueprintReflection(
    const TArray<FString>& Modules, const TArray<FString>& RequiredPaths,
    FP4NativeBlueprintReflection& Out, FString& Error);

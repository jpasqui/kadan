#pragma once
#include "CoreMinimal.h"

enum class EP4OriginalClassComparisonError : uint8
{ None, EditorThreadRequired, SourceUnreadable, DecodeFailed, ClassUnavailable, BlueprintUnavailable, CookFailed };

struct FP4OriginalClassValueNotHeld
{
    FString Object, Property, Kind, Schema, Reason, SourceValue;
};

struct KADAN_API FP4OriginalClassComparisonResult
{
    EP4OriginalClassComparisonError Error = EP4OriginalClassComparisonError::None;
    FString ErrorDetail, Package;
    bool Same = false, PackageKnown = false, ValuesKnown = false, TemplatesKnown = false, ComparedKnown = false;
    int32 ValuesNotHeld = 0, EmptyTemplates = 0, Objects = 0, Compared = 0, VariableIds = 0, Differences = 0;
    int32 CompilerFramesRebuilt = 0, EditorCompilerFramesRebuilt = 0;
    TArray<FString> OnlyGameSays, OnlyProjectSays;
    // Diagnostic evidence for the existing unsupported-value count. This does not
    // remove additional values or change the legacy comparison result.
    TArray<FP4OriginalClassValueNotHeld> ValuesNotHeldDetails;
    // Additional native source-custody facts; legacy JSON retains its schema.
    bool DefaultsReadable = false;
    // Bounded source/editor node evidence when default native payload is not understood.
    TArray<FString> UnreadableDefaultNodes;
    FString GameDefaultFactsDigest, DefaultFactsDigest;
    bool IsValid() const { return Error == EP4OriginalClassComparisonError::None; }
};

KADAN_API FP4OriginalClassComparisonResult P4CompareOriginalClassNative(const FString& HeaderFile, const FString& BodyFile);
KADAN_API FString P4OriginalClassComparisonJson(const FP4OriginalClassComparisonResult& Result);

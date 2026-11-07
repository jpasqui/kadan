#pragma once
#include "CoreMinimal.h"

struct FP4CapturedTextureGroup
{
    FString Name;
    int64 Value = 0;
};

struct FP4TextureProfileCapture
{
    TArray<FP4CapturedTextureGroup> Entries;
    FString DeclaredSourceSha256;
    FString DeclaredCapturedAt;
    FString DeclaredScope;
    bool RuntimeProvenanceAuthenticated = false;
    bool DefaultsProvenanceAuthenticated = false;
    bool CaptureAtomic = false;
};

struct FP4TextureProfileCaptureBudget
{
    uint64 MaxCharacters = 128ull * 1024 * 1024;
    uint64 MaxNotations = 12000000;
    uint32 MaxDepth = 128;
    uint32 MaxObjectKeys = 4096;
    uint32 MaxStringCharacters = 1024 * 1024;
    uint32 MaxEnumEntries = 4096;
};

// Input is the caller's losslessly decoded immutable UTF-8 capture snapshot.
// No file access, full DOM, UObjects, stock enums, runtime/default inference.
// Complete syntax/key/target record validation must finish before returning data.
KADAN_API bool P4ReadTextureProfileCapture(const FString& Snapshot, const FP4TextureProfileCaptureBudget& Budget,
    const TFunction<bool()>& Cancel, FP4TextureProfileCapture& Out, FString& Error);

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
bool P4TestTextureProfileCapture(FString& Failure, uint32& Checks);
#endif

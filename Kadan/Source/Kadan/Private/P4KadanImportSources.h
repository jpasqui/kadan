#pragma once
#include "CoreMinimal.h"

struct FP4KadanSourceAsset;

// Current-read source evidence. This is not a historic Blueprint source digest.
// Buffers are the exact bytes read and checked against the registered package.
struct FP4KadanSourceBinding
{
    FString Package, HeaderPath, BodyPath, SourceDigest;
    TArray<uint8> Header, Body;
};

// True also for a conflicting registry entry: its failed read must not fall
// back to a guessed file or a different cache.
KADAN_API bool P4KadanHasBoundSource(const FString& Package);
KADAN_API bool P4KadanReadBoundSource(const FString& Package, FP4KadanSourceBinding& Out, FString& Error);
KADAN_API bool P4KadanSourceBindingCurrent(const FP4KadanSourceBinding& Source, FString& Error);
// A digest of a bound source's current bytes, as a read and a currency check of it would give, made by Make
// once per kind for the bytes the import keeps. A check repeated through an import costs a file check only.
KADAN_API bool P4KadanBoundSourceDigest(const FString& Package, const FString& Kind,
    TFunctionRef<FString(const FP4KadanSourceBinding&)> Make, FString& Out, FString& Error);

// Only the caller's already scanned/acquired sources participate. Nested scopes
// restore the previous registry; no directory search, acquisition or asset load.
// The import that is running, as a number that changes when an import starts or ends (0 when none runs). Kadan's
// own folders only gain files during an import, so a source checked once in an import stays checked for the rest
// of it.
KADAN_API int64 P4KadanImportGeneration();

class KADAN_API FP4KadanImportSourceScope
{
public:
    explicit FP4KadanImportSourceScope(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources);
    ~FP4KadanImportSourceScope();
    void AddSources(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources);
    FP4KadanImportSourceScope(const FP4KadanImportSourceScope&) = delete;
    FP4KadanImportSourceScope& operator=(const FP4KadanImportSourceScope&) = delete;

private:
    friend KADAN_API bool P4KadanHasBoundSource(const FString& Package);
    friend KADAN_API bool P4KadanReadBoundSource(const FString& Package, FP4KadanSourceBinding& Out, FString& Error);
    friend KADAN_API bool P4KadanBoundSourceDigest(const FString& Package, const FString& Kind,
        TFunctionRef<FString(const FP4KadanSourceBinding&)> Make, FString& Out, FString& Error);
    // The import's kept read of a bound source, read first when it has none in this import.
    static const FP4KadanSourceBinding* Bound(const FString& Package, FString& Error);
    struct FPaths
    {
        FString Package, Header, Body;
    };
    TMap<FName, FPaths> Paths;
    TSet<FName> Ambiguous;
    FP4KadanImportSourceScope* Previous = nullptr;
};

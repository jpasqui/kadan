#pragma once
#include "CoreMinimal.h"

class FP4OriginalWorldSource;
using FP4OriginalWorldSourcePtr = TSharedPtr<const FP4OriginalWorldSource, ESPMode::ThreadSafe>;
// Exact caller-selected converted World pair, held immutable through consumption.
// No directory search, cache mutation, historic evidence or alternate source.
// Empty ExpectedPackage accepts only the canonical World identity decoded from
// these actual bytes; a supplied caller identity must match exactly.
KADAN_API bool P4ReadOriginalWorldSource(const FString& Header, const FString& Body, const FString& ExpectedPackage,
    FP4OriginalWorldSourcePtr& Out, FString& Error);
class KADAN_API FP4OriginalWorldSource
{
public:
    ~FP4OriginalWorldSource();
    bool Current(FString& Error) const;
    const FString& Package() const;
    const FString& HeaderPath() const;
    const FString& BodyPath() const;
    const FString& SourceDigest() const;
    const TArray<uint8>& Header() const;
    const TArray<uint8>& Body() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    explicit FP4OriginalWorldSource(TUniquePtr<FImpl>&&);
    friend KADAN_API bool P4ReadOriginalWorldSource(
        const FString&, const FString&, const FString&, FP4OriginalWorldSourcePtr&, FString&);
};

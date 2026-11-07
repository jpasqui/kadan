#pragma once
#include "CoreMinimal.h"
class UObject;

// A separate, conservative meaningful-content contract for native copies.
// Never grants original/reimport authority or changes original edit policy.
enum class EP4CopiedMICContent : uint8 { NotCopied, Equal, Different, Unreadable };
struct FP4CopiedMICContent
{
    EP4CopiedMICContent State = EP4CopiedMICContent::NotCopied;
    FString OriginalPackage, Error;
};
// Explicit Review only: bounded original lookup and temporary native cooks.
// Unreadable is a refusal, never "no matching game content".
KADAN_API FP4CopiedMICContent P4CompareCopiedMaterialInstanceContent(UObject* Asset);
